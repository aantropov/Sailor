#pragma once
#include <cstdio>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include "Sailor.h"
#include "Core/Singleton.hpp"
#include "Core/UniquePtr.hpp"

#define SAILOR_ENQUEUE_JOB(Name, Lambda) Sailor::JobSystem::Scheduler::GetInstance()->Run(Sailor::JobSystem::Scheduler::CreateJob(Name, Lambda))
#define SAILOR_ENQUEUE_JOB_RENDER_THREAD(Name, Lambda) Sailor::JobSystem::Scheduler::GetInstance()->Run(Sailor::JobSystem::Scheduler::CreateJob(Name, Lambda, Sailor::JobSystem::EThreadType::Rendering))

namespace Sailor
{
	namespace JobSystem
	{
		class WorkerThread;
		class Scheduler;

		enum class EThreadType : uint8_t
		{
			Rendering = 0,
			Worker = 1,
			FileSystem = 2
		};

		class IJob
		{
		public:

			virtual SAILOR_API float GetProgress() { return 0.0f; }
			virtual SAILOR_API bool IsFinished() const { return m_bIsFinished; }
			virtual SAILOR_API bool IsExecuting() const { return m_bIsStarted && !m_bIsFinished; }
			virtual SAILOR_API bool IsStarted() const { return m_bIsStarted; }

			virtual SAILOR_API void Complete();
			virtual SAILOR_API void Execute() = 0;

			virtual SAILOR_API ~IJob() = default;

			SAILOR_API const std::string& GetName() const { return m_name; }

			SAILOR_API bool AddDependency(IJob* job);

			// Wait other threads completion before start
			SAILOR_API void Join(const TWeakPtr<IJob>& job);
			SAILOR_API void Join(const std::vector<TWeakPtr<IJob>>& jobs);

			// Lock this thread while job is executing
			SAILOR_API void Wait();

			EThreadType GetThreadType() const { return m_threadType; }

		protected:

			SAILOR_API IJob(const std::string& name, EThreadType thread) : m_numBlockers(0), m_name(name), m_threadType(thread)
			{
			}

			std::atomic<bool> m_bIsFinished = false;
			std::atomic<bool> m_bIsStarted = false;
			std::atomic<bool> m_bIsInQueue = false;

			std::atomic<uint32_t> m_numBlockers;
			std::vector<IJob*> m_dependencies;
			std::string m_name;

			std::condition_variable m_onComplete;
			std::mutex m_waitMutex;
			std::mutex m_dependencyMutex;

			EThreadType m_threadType;
		};

		class Job : public IJob
		{
		public:

			virtual SAILOR_API ~Job() = default;

			SAILOR_API bool IsReadyToStart() const;

			virtual SAILOR_API void Execute() override;
			SAILOR_API Job(const std::string& name, const std::function<void()>& function, EThreadType thread);

		protected:

			std::function<void()> m_function;
		};

		class WorkerThread
		{
		public:

			SAILOR_API WorkerThread(
				std::string threadName,
				EThreadType threadType,
				std::condition_variable& refresh,
				std::mutex& mutex,
				std::list<TSharedPtr<Job>>& pJobsQueue);

			virtual SAILOR_API ~WorkerThread() = default;

			SAILOR_API WorkerThread(WorkerThread&& move) = delete;
			SAILOR_API WorkerThread(WorkerThread& copy) = delete;
			SAILOR_API WorkerThread& operator =(WorkerThread& rhs) = delete;

			SAILOR_API void ForcelyAssignJob(const TSharedPtr<Job>& pJob);

			SAILOR_API void Process();
			SAILOR_API void Join();

		protected:

			std::string m_threadName;
			TUniquePtr<std::thread> m_pThread;

			std::atomic_bool m_bIsBusy;
			TSharedPtr<Job> m_pJob;

			EThreadType m_threadType;

			// Assigned from scheduler
			std::condition_variable& m_refresh;
			std::mutex& m_mutex;
			std::list<TSharedPtr<Job>>& m_pJobsQueue;
		};

		class Scheduler final : public TSingleton<Scheduler>
		{
		public:

			static SAILOR_API void Initialize();

			virtual SAILOR_API ~Scheduler() override;

			uint32_t SAILOR_API GetNumWorkerThreads() const;
			uint32_t SAILOR_API GetNumRenderingJobs() const;

			static SAILOR_API TSharedPtr<Job> CreateJob(const std::string& name, const std::function<void()>& lambda, EThreadType thread = EThreadType::Worker);
			SAILOR_API void Run(const TSharedPtr<Job>& pJob);

			SAILOR_API bool TryFetchNextAvailiableJob(TSharedPtr<Job>& pOutJob, EThreadType threadType);

			SAILOR_API void NotifyWorkerThread(EThreadType threadType, bool bNotifyAllThreads = false);

		protected:

			SAILOR_API void GetThreadSyncVarsByThreadType(
				EThreadType threadType,
				std::mutex*& pOutMutex,
				std::list<TSharedPtr<Job>>*& pOutQueue,
				std::condition_variable*& pOutCondVar);

			SAILOR_API Scheduler() = default;

			std::mutex m_queueMutex[3];
			std::condition_variable m_refreshCondVar[3];
			std::list<TSharedPtr<Job>> m_pJobsQueue[3];

			std::atomic<uint32_t> m_numBusyThreads;
			std::vector<WorkerThread*> m_workerThreads;
			std::atomic_bool m_bIsTerminating;

			friend class WorkerThread;
		};
	}
}