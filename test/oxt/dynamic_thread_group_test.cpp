#include "tut.h"
#include <boost/bind.hpp>
#include <boost/thread/thread_time.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <oxt/system_calls.hpp>
#include <unistd.h>

using namespace boost;
using namespace oxt;

namespace tut {
	struct dynamic_thread_group_test {
		dynamic_thread_group group;
	};
	
	DEFINE_TEST_GROUP(dynamic_thread_group_test);
	
	struct Counter {
		struct timeout_expired { };
		
		unsigned int value;
		boost::mutex mutex;
		boost::condition_variable cond;
		
		Counter() {
			value = 0;
		}
		
		void wait_until(unsigned int wanted_value, unsigned int timeout = 1000) {
			boost::unique_lock<boost::mutex> l(mutex);
			while (value < wanted_value) {
				if (!cond.timed_wait(l, get_system_time() + posix_time::milliseconds(timeout))) {
					throw timeout_expired();
				}
			}
		}
		
		void increment() {
			boost::unique_lock<boost::mutex> l(mutex);
			value++;
			cond.notify_all();
		}
	};
	
	TEST_METHOD(1) {
		// It has 0 threads in the beginning.
		ensure_equals(group.num_threads(), 0u);
	}
	
	static void wait_until_done(Counter *parent_counter, Counter *child_counter) {
		// Tell parent thread that this thread has started.
		parent_counter->increment();
		
		// Wait until parent says that we can quit.
		child_counter->wait_until(1);
	}
	
	TEST_METHOD(2) {
		// Test whether newly created threads are added to the thread
		// group, and whether they are automatically removed from the
		// thread group upon termination.
		
		// Start 3 'f' threads.
		Counter f_parent_counter, f_child_counter;
		boost::function<void()> f(boost::bind(wait_until_done, &f_parent_counter, &f_child_counter));
		group.create_thread(f);
		group.create_thread(f);
		group.create_thread(f);
		
		// Start 1 'g' thread.
		Counter g_parent_counter, g_child_counter;
		boost::function<void()> g(boost::bind(wait_until_done, &g_parent_counter, &g_child_counter));
		group.create_thread(g);
		
		f_parent_counter.wait_until(3); // Wait until all 'f' threads have started.
		g_parent_counter.wait_until(1); // Wait until the 'g' thread has started.
		
		ensure_equals("There are 4 threads in the group", group.num_threads(), 4u);
		
		// Tell all 'f' threads that they can quit now.
		f_child_counter.increment();
		usleep(10000);
		ensure_equals(group.num_threads(), 1u);
		
		// Tell the 'g' thread that it can quit now.
		g_child_counter.increment();
		usleep(10000);
		ensure_equals(group.num_threads(), 0u);
	}
	
	static void sleep_and_set_true(Counter *counter, bool *flag) {
		// Tell parent thread that this thread has started.
		counter->increment();
		try {
			syscalls::usleep(5000000);
		} catch (thread_interrupted &) {
			*flag = true;
		}
	}
	
	TEST_METHOD(3) {
		// interrupt_and_join_all() works.
		
		// Create two threads.
		Counter counter;
		bool flag1 = false, flag2 = false;
		boost::function<void ()> f(boost::bind(sleep_and_set_true, &counter, &flag1));
		boost::function<void ()> g(boost::bind(sleep_and_set_true, &counter, &flag2));
		group.create_thread(f);
		group.create_thread(g);
		// Wait until both threads have started.
		counter.wait_until(2);
		
		// Now interrupt and join them.
		group.interrupt_and_join_all();
		// Both threads should have received a thread interruption
		// request and terminated as a result.
		ensure_equals(flag1, true);
		ensure_equals(flag2, true);
		ensure_equals(group.num_threads(), 0u);
	}
	
	static void do_nothing(unsigned int max) {
		unsigned int i;
		for (i = 0; i < max; i++) { }
	}
	
	static void create_threads(dynamic_thread_group *group) {
		for (int i = 1000; i >= 0; i--) {
			boost::function<void ()> f(boost::bind(do_nothing, i * 1000));
			group->create_thread(f);
		}
	}
	
	static void interrupt_group(dynamic_thread_group *group) {
		for (int i = 0; i < 1000; i++) {
			group->interrupt_and_join_all();
		}
	}
	
	TEST_METHOD(4) {
		// Stress test.
		oxt::thread thr1(boost::bind(create_threads, &group));
		oxt::thread thr2(boost::bind(interrupt_group, &group));
		thr1.join();
		thr2.join();
		group.interrupt_and_join_all();
		ensure_equals(group.num_threads(), 0u);
	}
	
	TEST_METHOD(5) {
		// If the thread function crashes then it will still be correctly removed from the pool.
	}
}