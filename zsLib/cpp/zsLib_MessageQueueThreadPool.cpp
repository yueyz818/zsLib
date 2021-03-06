/*

 Copyright (c) 2014, Robin Raymond
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those
 of the authors and should not be interpreted as representing official policies,
 either expressed or implied, of the FreeBSD Project.
 
 */

#include <zsLib/internal/zsLib_MessageQueueThreadPool.h>
#include <zsLib/internal/zsLib_MessageQueue.h>

#include <zsLib/Event.h>
#include <zsLib/Log.h>

//namespace zsLib { ZS_DECLARE_SUBSYSTEM(zslib) }

namespace zsLib
{
  namespace internal
  {
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //
    // MessageQueueThreadPoolDispatcherThread
    //

    class MessageQueueThreadPoolDispatcherThread
    {
    protected:
      class make_private {};

    protected:
      //-----------------------------------------------------------------------
      MessageQueueThreadPoolDispatcherThread(
        const make_private &,
        const char *threadName
      ) :
        mThreadName(threadName)
      {
      }

    public:
      //-----------------------------------------------------------------------
      static MessageQueueThreadPoolDispatcherThreadPtr create(
        MessageQueueThreadPoolPtr pool,
        const char *threadName = NULL,
        ThreadPriorities threadPriority = ThreadPriority_Normal
      )
      {
        MessageQueueThreadPoolDispatcherThreadPtr pThis(new MessageQueueThreadPoolDispatcherThread(make_private{}, threadName));
        pThis->mThisWeak = pThis;
        pThis->mPool = pool;
        pThis->mThreadPriority = threadPriority;
        pThis->mThread = ThreadPtr(new std::thread(std::ref(*pThis.get())));

        zsLib::setThreadPriority(*(pThis->mThread), threadPriority);
        return pThis;
      }

      //-----------------------------------------------------------------------
      void operator () ()
      {
        debugSetCurrentThreadName(mThreadName);

        do
        {
          if (mMustShutdown) goto done;

          {
            auto pool = mPool.lock();
            if (!pool) goto done;

            pool->notifyIdle(mThisWeak.lock());
          }

          mEvent.wait();

          {
            auto pool = mPool.lock();
            if (!pool) goto done;
            pool->processOneQueue();
          }

          if (mMustShutdown) goto done;

        } while (!mMustShutdown);

      done:
        {
          mIsShutdown = true;
        }
      }

      //-----------------------------------------------------------------------
      void waitForShutdown()
      {
        ThreadPtr thread;
        {
          AutoLock lock(mLock);
          thread = mThread;

          mMustShutdown = true;
          mEvent.notify();
        }

        if (!thread)
          return;

        thread->join();

        {
          AutoLock lock(mLock);
          mThread.reset();
        }
      }

      //-------------------------------------------------------------------------
      void setThreadPriority(ThreadPriorities threadPriority)
      {
        AutoLock lock(mLock);
        if (!mThread) return;

        if (threadPriority == mThreadPriority) return;

        mThreadPriority = threadPriority;

        zsLib::setThreadPriority(*mThread, threadPriority);
      }

      //-----------------------------------------------------------------------
      void notify()
      {
        mEvent.notify();
      }

      //-----------------------------------------------------------------------
      bool isCurrentThread() const noexcept
      {
        AutoLock lock(mLock);
        if (!mThread)
          return false;
        return mThread->get_id() == std::this_thread::get_id();
      }

    protected:
      MessageQueueThreadPoolDispatcherThreadWeakPtr mThisWeak;

      ThreadPtr mThread;
      String mThreadName;

      mutable zsLib::Event mEvent {zsLib::Event::Reset_Auto};

      mutable Lock mLock;
      std::atomic_bool mMustShutdown{};
      ThreadPriorities mThreadPriority{ ThreadPriority_Normal };

      std::atomic_bool mIsShutdown{};

      MessageQueueThreadPoolWeakPtr mPool;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //
    // MessageQueueThreadPoolQueueNotifier
    //

    class MessageQueueThreadPoolQueueNotifier : public IMessageQueueNotify
    {
    protected:
      class make_private {};

    public:
      //-----------------------------------------------------------------------
      MessageQueueThreadPoolQueueNotifier(
        const make_private &,
        MessageQueueThreadPoolPtr pool
      ) noexcept :
        mPool(pool)
      {
      }

      ~MessageQueueThreadPoolQueueNotifier() noexcept
      {
      }

    public:
      //-----------------------------------------------------------------------
      static MessageQueueThreadPoolQueueNotifierPtr create(MessageQueueThreadPoolPtr pool) noexcept  {
        MessageQueueThreadPoolQueueNotifierPtr pThis(make_shared<MessageQueueThreadPoolQueueNotifier>(make_private{}, pool));
        pThis->mThisWeak = pThis;
        pThis->init();
        return pThis;
      }

      //-----------------------------------------------------------------------
      IMessageQueuePtr getMessageQueue() noexcept
      {
        auto queue = mQueue;
        mQueue.reset();
        return queue;
      }

      //-----------------------------------------------------------------------
      void processQueue() noexcept
      {
        auto queue = mQueueWeak.lock();
        if (queue) {
          queue->process();
        }

        mPosted.exchange(false);

        if (!queue) return;
        if (queue->getTotalUnprocessedMessages() < 1) return;

        notifyMessagePosted();
      }

    protected:
      //-----------------------------------------------------------------------
      void init() noexcept
      {
        mQueue = MessageQueue::create(mThisWeak.lock());
        mQueueWeak = mQueue;
      }

      //-----------------------------------------------------------------------
      //
      // MessageQueueThreadPoolQueue => IMessageQueueNotify
      //

      //-----------------------------------------------------------------------
      void notifyMessagePosted() noexcept override;

      bool isCurrentThread() const noexcept override;

    protected:
      MessageQueueThreadPoolQueueNotifierWeakPtr mThisWeak;

      MessageQueuePtr mQueue;
      MessageQueueWeakPtr mQueueWeak;

      MessageQueueThreadPoolPtr mPool;

      std::atomic<bool> mPosted{ false };
    };

    //-------------------------------------------------------------------------
    void MessageQueueThreadPoolQueueNotifier::notifyMessagePosted() noexcept
    {
      bool posted = mPosted.exchange(true);
      if (posted) return;

      mPool->notifyPosted(mThisWeak.lock());
    }

    //-------------------------------------------------------------------------
    bool MessageQueueThreadPoolQueueNotifier::isCurrentThread() const noexcept
    {
      return mPool->isCurrentThread();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //
    // MessageQueueThreadPool
    //

    //-------------------------------------------------------------------------
    MessageQueueThreadPool::MessageQueueThreadPool(const make_private &) noexcept
    {
    }

    //-------------------------------------------------------------------------
    MessageQueueThreadPool::~MessageQueueThreadPool() noexcept
    {
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::init() noexcept
    {
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::notifyPosted(MessageQueueThreadPoolQueueNotifierPtr queue) noexcept
    {
      MessageQueueThreadPoolDispatcherThreadPtr idle;

      {
        AutoLock lock(mLock);
        mPendingQueues.push(queue);

        if (mIdleThreads.size() < 1) {
          ++mMissingIdle;
          return;
        }

        idle = mIdleThreads.front();
        mIdleThreads.pop();
      }

      idle->notify();
    }

    //-------------------------------------------------------------------------
    bool MessageQueueThreadPool::isCurrentThread() const noexcept
    {
        AutoLock lock(mLock);

        for (auto iter = mThreads.begin(); iter != mThreads.end(); ++iter) {
          auto thread = (*iter);

          if (thread->isCurrentThread())
            return true;
        }
        return false;
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::notifyIdle(MessageQueueThreadPoolDispatcherThreadPtr dispatcher) noexcept
    {
      {
        AutoLock lock(mLock);

        if (mMissingIdle < 1) {
          mIdleThreads.push(dispatcher);
          return;
        }
        --mMissingIdle;
      }

      dispatcher->notify();
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::processOneQueue() noexcept
    {
      MessageQueueThreadPoolQueueNotifierPtr notifier;

      {
        AutoLock lock(mLock);

        if (mPendingQueues.size() < 1) return;

        notifier = mPendingQueues.front();
        mPendingQueues.pop();
      }

      notifier->processQueue();
    }

    //-------------------------------------------------------------------------
    MessageQueueThreadPoolPtr MessageQueueThreadPool::create() noexcept
    {
      MessageQueueThreadPoolPtr pThis(make_shared<MessageQueueThreadPool>(make_private{}));
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::createThread(
      const char *threadName,
      ThreadPriorities threadPriority
    ) noexcept
    {
      MessageQueueThreadPoolDispatcherThreadPtr dispatcher = MessageQueueThreadPoolDispatcherThread::create(mThisWeak.lock(), threadName, threadPriority);

      AutoLock lock(mLock);
      mThreads.push_back(dispatcher);
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::waitForShutdown() noexcept
    {
      while (true)
      {
        DispatcherThreadList threads;

        {
          AutoLock lock(mLock);
          threads = mThreads;
          mThreads.clear();
        }

        if (threads.size() < 1) return;

        for (auto iter = threads.begin(); iter != threads.end(); ++iter) {
          auto thread = (*iter);

          thread->waitForShutdown();
        }
      }
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadPool::setThreadPriority(ThreadPriorities threadPriority) noexcept
    {
      DispatcherThreadList threads;

      {
        AutoLock lock(mLock);
        threads = mThreads;
      }

      if (threads.size() < 1) return;

      for (auto iter = threads.begin(); iter != threads.end(); ++iter) {
        auto thread = (*iter);

        thread->setThreadPriority(threadPriority);
      }
    }

    //-------------------------------------------------------------------------
    bool MessageQueueThreadPool::hasPendingMessages() noexcept
    {
      AutoLock lock(mLock);
      return mPendingQueues.size() > 0;
    }

    //-------------------------------------------------------------------------
    IMessageQueuePtr MessageQueueThreadPool::createQueue() noexcept
    {
      MessageQueueThreadPoolQueueNotifierPtr notifier = MessageQueueThreadPoolQueueNotifier::create(mThisWeak.lock());
      return notifier->getMessageQueue();
    }

  } // namespace internal

  //---------------------------------------------------------------------------
  IMessageQueueThreadPoolPtr IMessageQueueThreadPool::create() noexcept
  {
    return internal::MessageQueueThreadPool::create();
  }

} // namespace zsLib

