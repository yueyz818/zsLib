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

#include <zsLib/types.h>
#include <zsLib/internal/platform.h>

#if defined(WINUWP) && defined(__cplusplus_winrt)

#include <zsLib/internal/zsLib_MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP.h>
#include <zsLib/internal/zsLib_MessageQueueThreadBasic.h>
#include <zsLib/internal/zsLib_MessageQueue.h>
#include <zsLib/Log.h>
#include <zsLib/helpers.h>
#include <zsLib/Stringize.h>
#include <zsLib/Singleton.h>

#include <Windows.h>
#include <tchar.h>

using namespace Windows::UI::Core;

namespace zsLib { ZS_DECLARE_SUBSYSTEM(zslib) }

namespace zsLib
{
  namespace internal
  {

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------


    //-------------------------------------------------------------------------
    MessageQueueThreadPtr MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::singleton() noexcept
    {
      CoreDispatcher ^dispatcher = setupDispatcher();

      if (nullptr == dispatcher) {
        static SingletonLazySharedPtr<MessageQueueThreadBasic> singleton(MessageQueueThreadBasic::create("zsLib.winuwp.backgroundDispatcher"));
        return singleton.singleton();
      }

      static SingletonLazySharedPtr<MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP> singleton(MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::create(dispatcher));
      return singleton.singleton();
    }

    //-------------------------------------------------------------------------
    Windows::UI::Core::CoreDispatcher ^MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::setupDispatcher(CoreDispatcher ^dispatcher) noexcept
    {
      static CoreDispatcher ^gDispatcher = dispatcher;
      return gDispatcher;
    }

    //-------------------------------------------------------------------------
    bool MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::hasDispatcher(bool ready) noexcept
    {
      static std::atomic<bool> isSetup{ false };
      if (ready) isSetup = true;
      return isSetup;
    }

    //-------------------------------------------------------------------------
    MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWPPtr MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::create(CoreDispatcher ^dispatcher) noexcept
    {
      MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWPPtr thread(new MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP);
      thread->mThisWeak = thread;
      thread->mQueue = MessageQueue::create(thread);
      thread->mDispatcher = dispatcher;
      ZS_ASSERT(thread->mDispatcher);
      return thread;
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::dispatch(MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWPPtr queue) noexcept
    {
      queue->process();
    }

    //-------------------------------------------------------------------------
    MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP() noexcept
    {
    }

    //-------------------------------------------------------------------------
    MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::~MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP() noexcept
    {
      mThisWeak.reset();
      mDispatcher = nullptr;
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::process() noexcept
    {
      mQueue->processOnlyOneMessage(); // process only one message at a time since this must be syncrhonized through the GUI message queue
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::processMessagesFromThread() noexcept
    {
      mQueue->process();
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::post(IMessageQueueMessageUniPtr message) noexcept(false)
    {
      if (mIsShutdown) {
        ZS_THROW_CUSTOM(IMessageQueue::Exceptions::MessageQueueGone, "message posted to message queue after message queue was deleted.")
      }
      mQueue->post(std::move(message));
    }

    //-------------------------------------------------------------------------
    IMessageQueue::size_type MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::getTotalUnprocessedMessages() const noexcept
    {
      return mQueue->getTotalUnprocessedMessages();
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::notifyMessagePosted() noexcept
    {
      CoreDispatcher ^dispatcher;
      MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWPPtr queue;

      {
        AutoLock lock(mLock);

        ZS_ASSERT(nullptr != mDispatcher);

        dispatcher = mDispatcher;
        queue = mThisWeak.lock();
      }

      auto callback = [queue] () {dispatch(queue);};

      dispatcher->RunAsync(
        Windows::UI::Core::CoreDispatcherPriority::Normal,
        ref new Windows::UI::Core::DispatchedHandler(callback)
      );
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::waitForShutdown() noexcept
    {
      mIsShutdown = true;
    }

    //-------------------------------------------------------------------------
    void MessageQueueThreadUsingCurrentGUIMessageQueueForWinUWP::setThreadPriority(ZS_MAYBE_USED() ThreadPriorities threadPriority) noexcept
    {
      ZS_MAYBE_USED(threadPriority);
      // no-op
    }
  }
}

#endif // defined(WINUWP) && defined(__cplusplus_winrt)

