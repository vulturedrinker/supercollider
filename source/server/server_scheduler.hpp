//  server scheduler
//  Copyright (C) 2008 Tim Blechmann
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#ifndef SERVER_SCHEDULER_HPP
#define SERVER_SCHEDULER_HPP

#include "dsp_thread.hpp"
#include "group.hpp"
#include "memory_pool.hpp"
#include "utilities/branch_hints.hpp"
#include "utilities/callback_system.hpp"
#include "utilities/static_pooled_class.hpp"

#include "nova-tt/thread_affinity.hpp"
#include "nova-tt/thread_priority.hpp"

namespace nova
{

/** audio-thread synchronization callback
 *
 *  callback for non-rt to rt thread synchronization. since it is using
 *  a locked internal memory pool, instances should not be allocated
 *  from the real-time thread
 *
 * */
struct audio_sync_callback:
    public static_pooled_class<audio_sync_callback, 1<<20 /* 1mb pool of realtime memory */,
                               true>
{
    virtual ~audio_sync_callback()
    {}

    virtual void run(void) = 0;
};

/** dsp thread init functor
 *
 *  for the real-time use, it should acquire real-time scheduling and pin the thread to a certain CPU
 *
 * */
struct thread_init_functor
{
    thread_init_functor(bool real_time):
        rt(real_time)
    {}

    void operator()(int thread_index)
    {
        if (rt)
        {
            int min, max;
            boost::tie(min, max) = thread_priority_interval_rt();
            int priority = max - 3;
            priority = std::max(min, priority);

            thread_set_priority_rt(priority);
        }

        if (!thread_set_affinity(thread_index))
            std::cerr << "Warning: cannot set thread affinity of dsp thread" << std::endl;
    }

private:
    bool rt;
};


/** scheduler class of the nova server
 *
 *  - provides a callback system to place callbacks in the scheduler
 *  - manages dsp threads, which themselves manage the dsp queue interpreter
 *
 * */
class scheduler
{
    typedef nova::dsp_threads<queue_node, thread_init_functor, rt_pool_allocator<void*> > dsp_threads;

    struct reset_queue_cb:
        public audio_sync_callback
    {
    public:
        reset_queue_cb(scheduler * sched,
                       dsp_threads::dsp_thread_queue_ptr & qptr):
            sched(sched), qptr(qptr)
        {}

        void run(void)
        {
            sched->reset_queue_sync(qptr);
            /** todo: later free the queue in a helper thread */
        }

    private:
        scheduler * sched;
        dsp_threads::dsp_thread_queue_ptr qptr;
    };

protected:
    /* called from the driver callback */
    void reset_queue_sync(dsp_threads::dsp_thread_queue_ptr & qptr)
    {
        threads.reset_queue(qptr);
    }

public:
    /* start thread_count - 1 scheduler threads */
    scheduler(dsp_threads::thread_count_t thread_count = 1, bool realtime = false):
        threads(thread_count, thread_init_functor(realtime))
    {
        threads.start_threads();
    }

    ~scheduler(void)
    {
        cbs.run_callbacks();
        threads.terminate_threads();
    }

    void add_sync_callback(audio_sync_callback * cb)
    {
        cbs.add_callback(cb);
    }

    /* called from the audio driver */
    void operator()(void);

    /* schedule to set a new queue */
    void reset_queue(dsp_threads::dsp_thread_queue_ptr & qptr)
    {
        add_sync_callback(new reset_queue_cb(this, qptr));
    }

private:
    callback_system<audio_sync_callback> cbs;
    dsp_threads threads;
};

} /* namespace nova */

#endif /* SERVER_SCHEDULER_HPP */

