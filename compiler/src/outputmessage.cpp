/**
 * Tibia GIMUD Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2017  Alejandro Mujica <alejandrodemujica@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "outputmessage.h"
#include "protocol.h"
#include "lockfree.h"
#include "scheduler.h"

extern Scheduler g_scheduler;

const uint16_t OUTPUTMESSAGE_FREE_LIST_CAPACITY = 2048;
const std::chrono::milliseconds OUTPUTMESSAGE_AUTOSEND_DELAY {10};

class OutputMessageAllocator
{
public:
    typedef OutputMessage value_type;
    
    OutputMessageAllocator() = default;
    
    template<typename U>
    constexpr OutputMessageAllocator(const OutputMessageAllocator&) noexcept {}
    
    template<typename U>
    struct rebind {
        typedef typename std::conditional<
            std::is_same<U, OutputMessage>::value,
            OutputMessageAllocator,
            LockfreePoolingAllocator<U, OUTPUTMESSAGE_FREE_LIST_CAPACITY>
        >::type other;
    };
    
    OutputMessage* allocate(size_t n) const {
        if (n != 1) {
            throw std::bad_alloc();
        }
        
        OutputMessage* p;
        if (!getFreeList().pop(p)) {
            p = static_cast<OutputMessage*>(operator new(sizeof(OutputMessage)));
        }
        return p;
    }
    
    void deallocate(OutputMessage* p, size_t n) const {
        if (n != 1 || !p) {
            return;
        }
        
        if (!getFreeList().bounded_push(p)) {
            operator delete(p);
        }
    }
    
    bool operator==(const OutputMessageAllocator&) const noexcept {
        return true;
    }
    
    bool operator!=(const OutputMessageAllocator&) const noexcept {
        return false;
    }

private:
    typedef boost::lockfree::stack<OutputMessage*, boost::lockfree::capacity<OUTPUTMESSAGE_FREE_LIST_CAPACITY>> FreeList;
    
    static FreeList& getFreeList() {
        static FreeList freeList;
        return freeList;
    }
};

void OutputMessagePool::scheduleSendAll()
{
	auto functor = std::bind(&OutputMessagePool::sendAll, this);
	g_scheduler.addEvent(createSchedulerTask(OUTPUTMESSAGE_AUTOSEND_DELAY.count(), functor));
}

void OutputMessagePool::sendAll()
{
	//dispatcher thread
	for (auto& protocol : bufferedProtocols) {
		auto& msg = protocol->getCurrentBuffer();
		if (msg) {
			protocol->send(std::move(msg));
		}
	}

	if (!bufferedProtocols.empty()) {
		scheduleSendAll();
	}
}

void OutputMessagePool::addProtocolToAutosend(Protocol_ptr protocol)
{
	//dispatcher thread
	if (bufferedProtocols.empty()) {
		scheduleSendAll();
	}
	bufferedProtocols.emplace_back(protocol);
}

void OutputMessagePool::removeProtocolFromAutosend(const Protocol_ptr& protocol)
{
	//dispatcher thread
	auto it = std::find(bufferedProtocols.begin(), bufferedProtocols.end(), protocol);
	if (it != bufferedProtocols.end()) {
		std::swap(*it, bufferedProtocols.back());
		bufferedProtocols.pop_back();
	}
}

OutputMessage_ptr OutputMessagePool::getOutputMessage()
{
	return std::allocate_shared<OutputMessage>(OutputMessageAllocator());
}