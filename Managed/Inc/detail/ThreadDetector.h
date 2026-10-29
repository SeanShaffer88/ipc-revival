#pragma once

#include <msclr/auto_handle.h>


namespace IPC
{
namespace Managed
{
    namespace detail
    {
        ref class ThreadDetector
        {

        public:
            System::IDisposable^ Detect();

            property bool IsExpectedThread
            {
                bool get();
            }

        private:
            ref class Detector;

            System::Threading::ThreadLocal<bool> m_isExpected;
        };

    } // detail

} // Managed
} // IPC
