#ifndef __THREADPOOL
#define __THREADPOOL

#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <functional>

// Heavy multithreading only for X86 based systems
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define THREAD_BY_CORE 2
#else
#define THREAD_BY_CORE 1
#endif

namespace Utils
{
	class ThreadPool
	{
	public:
		typedef std::function<void(void)> work_function;

		ThreadPool(int threadByCore = THREAD_BY_CORE);
		~ThreadPool();

		void start();
		void queueWorkItem(work_function work);
		void wait();
		void wait(work_function work, int delay = 50);
		void cancel() { mRunning = false; }
		void stop();

		bool isRunning() { return mRunning; }

	private:
		bool mRunning;
		bool mWaiting;
		std::queue<work_function> mWorkQueue;
		std::atomic<size_t> mNumWork;
		std::mutex _mutex;
		std::vector<std::thread> mThreads;
		int mThreadByCore;

	};
}

#endif
