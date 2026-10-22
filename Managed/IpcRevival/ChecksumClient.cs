using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory | DllImportSearchPath.SafeDirectories)]
namespace IpcRevival
{
    [StructLayout(LayoutKind.Sequential)]
    public struct ChecksumResult { public ulong RequestId; public ulong Size; public ulong Checksum; }
    internal sealed class SessionHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public SessionHandle() : base(true) { }
        protected override bool ReleaseHandle() { Native.ipc_close(handle); return true; }
    }
    internal static class Native
    {
        private const string Library="ipc_bridge";
        [DllImport(Library, ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
        internal static extern uint ipc_abi_version();
        [DllImport(Library, ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
        internal static extern uint ipc_broker_id(SessionHandle session);
        [DllImport(Library, ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
        internal static extern int ipc_open([MarshalAs(UnmanagedType.LPStr)] string endpoint, uint timeout, out SessionHandle session);
        [DllImport(Library, ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
        internal static extern int ipc_hash(SessionHandle session, [In] byte[] bytes, uint size, uint timeout, out ChecksumResult result);
        [DllImport(Library, ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
        internal static extern void ipc_close(IntPtr session);
    }
    /// <summary>A bounded, process-isolated client for the included native checksum worker.</summary>
    public sealed class ChecksumClient : IDisposable
    {
        public const int MaximumPayload=256*1024;
        private readonly SessionHandle session;
        public uint BrokerProcessId { get { if(session.IsClosed) throw new ObjectDisposedException(nameof(ChecksumClient)); return Native.ipc_broker_id(session); } }
        public ChecksumClient(string endpoint,uint timeoutMilliseconds=5000)
        {
            if(endpoint==null) throw new ArgumentNullException(nameof(endpoint));
            if(endpoint.Length==0||endpoint.Length>80) throw new ArgumentException("Endpoint length must be 1..80",nameof(endpoint));
            foreach(char c in endpoint)
                if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) throw new ArgumentException("Endpoint must be an ASCII identifier",nameof(endpoint));
            ValidateTimeout(timeoutMilliseconds);
            if(Native.ipc_abi_version()!=1) throw new NotSupportedException("Unsupported native bridge ABI");
            int status=Native.ipc_open(endpoint,timeoutMilliseconds,out session);
            if(status!=0) { session.Dispose(); Throw(status); }
        }
        public ChecksumResult Hash(byte[] bytes,uint timeoutMilliseconds=2000)
        {
            if(bytes==null) throw new ArgumentNullException(nameof(bytes));
            if(bytes.Length>MaximumPayload) throw new ArgumentOutOfRangeException(nameof(bytes));
            ValidateTimeout(timeoutMilliseconds);
            if(session.IsClosed) throw new ObjectDisposedException(nameof(ChecksumClient));
            int status=Native.ipc_hash(session,bytes,(uint)bytes.Length,timeoutMilliseconds,out var result);
            if(status!=0) Throw(status);
            return result;
        }
        private static void ValidateTimeout(uint timeout)
        { if(timeout<1||timeout>30000) throw new ArgumentOutOfRangeException(nameof(timeout)); }
        private static void Throw(int status)
        {
            switch(status)
            {
                case 1: throw new ArgumentException("Native bridge rejected arguments");
                case 2: throw new TimeoutException("IPC deadline exceeded; a failed broker is terminated. Reopen the client before retrying.");
                case 3: throw new IOException("IPC peer disconnected or violated the protocol");
                case 4: throw new IOException("Native resource acquisition failed");
                case 6: throw new InvalidOperationException("IPC session is closed after an earlier failure");
                default: throw new IOException("Native IPC failure: "+status);
            }
        }
        public void Dispose() { session.Dispose(); }
    }
}
