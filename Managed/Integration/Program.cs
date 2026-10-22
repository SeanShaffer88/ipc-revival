using System;
using System.Threading.Tasks;
using IpcRevival;
internal static class Program
{
    static ulong Hash(byte[] bytes) { ulong hash=14695981039346656037UL; foreach(byte b in bytes) hash=unchecked((hash^b)*1099511628211UL); return hash; }
    static void Check(bool value) { if(!value) throw new Exception("check failed"); }
    public static int Main(string[] args)
    {
        try
        {
            if(args.Length!=2) return 1;
            if(args[0]=="timeout")
            {
                using var client=new ChecksumClient(args[1]);
                bool failed=false;try {client.Hash(new byte[4096],150);}catch(TimeoutException){failed=true;}
                Check(failed);failed=false;try {client.Hash(new byte[1]);}catch(InvalidOperationException){failed=true;}
                Check(failed);Console.WriteLine("PASS timeout and closed session");return 0;
            }
            if(args[0]=="peer")
            {
                using var client=new ChecksumClient(args[1]);Console.WriteLine("READY");Console.WriteLine("BROKER "+client.BrokerProcessId);Console.ReadLine();
                try { client.Hash(new byte[4096],500);return 2; }catch(System.IO.IOException){Console.WriteLine("PASS peer death");return 0;}catch(TimeoutException){Console.WriteLine("PASS bounded peer timeout");return 0;}
            }
            if(args[0]!="run")return 1;
            using(var client=new ChecksumClient(args[1]))
            {
                foreach(int size in new[]{0,1,1024,65536,262144})
                {
                    var bytes=new byte[size];for(int i=0;i<size;++i)bytes[i]=(byte)(i*31+7);
                    for(int n=0;n<20;++n) { var r=client.Hash(bytes);Check(r.Size==(ulong)size&&r.Checksum==Hash(bytes)); }
                }
                Parallel.For(0,32,i=>{var b=new byte[]{(byte)i};Check(client.Hash(b).Checksum==Hash(b));});
                bool invalid=false;try {client.Hash(new byte[ChecksumClient.MaximumPayload+1]);}catch(ArgumentOutOfRangeException){invalid=true;}Check(invalid);
                GC.Collect();GC.WaitForPendingFinalizers();Check(client.Hash(new byte[0]).Size==0);
            }
            var closed=new ChecksumClient(args[1]);closed.Dispose();closed.Dispose();
            bool disposed=false;try{closed.Hash(new byte[1]);}catch(ObjectDisposedException){disposed=true;}Check(disposed);
            Console.WriteLine("PASS .NET 10 interop, bounds, concurrency, GC and disposal");return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
