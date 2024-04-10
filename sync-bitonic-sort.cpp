#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::this_thread::sleep_for;
using TimePoint = std::chrono::steady_clock::time_point;
using TimeSpan  = std::chrono::duration<double>;

class Barrier {
    public:
    Barrier(ptrdiff_t expected) : expected_count(expected), current_count(0), phase(0) {
    }

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mutex);
        current_count++;
        if (current_count == expected_count) {
            // All threads have arrived, release them
            current_count = 0;
            phase++;
            cv.notify_all();
        } else {
            size_t my_phase = phase;
            cv.wait(lock, [this, my_phase] { return phase != my_phase; });
        }
    }

    private:
    std::mutex              mutex;
    std::condition_variable cv;
    ptrdiff_t               expected_count;
    ptrdiff_t               current_count;
    size_t                  phase;
};

void compare_exchange(int64_t &a, int64_t &b, bool dir) {
    if ((a > b) == dir) {
        std::swap(a, b);
    }
}

void bitonic_merge(int64_t *data, size_t size, size_t start, size_t end, bool dir, Barrier &barrier) {
    if (end > 1) {
        size_t half = end / 2;
        for (size_t i = start; i < start + half; ++i) {
            compare_exchange(data[i], data[i + half], dir);
        }
        bitonic_merge(data, size, start, half, dir, barrier);
        bitonic_merge(data, size, start + half, half, dir, barrier);
    }
    barrier.arrive_and_wait();
}

void bitonic_sort_recursive(int64_t *data, size_t size, size_t start, size_t end, bool dir, Barrier &barrier) {
    if (end > 1) {
        size_t half = end / 2;
        bitonic_sort_recursive(data, size, start, half, !dir, barrier);
        bitonic_sort_recursive(data, size, start + half, half, dir, barrier);
        bitonic_merge(data, size, start, end, dir, barrier);
    }
}

void parallel_bitonic_sort(int64_t *data, size_t size, size_t thread_count) {
    // Divide the work among threads
    size_t elements_per_thread = size / thread_count;

    // Create an array of threads
    std::vector<std::thread> threads(thread_count);

    // Create your custom Barrier with the expected number of threads
    Barrier barrier(thread_count);

    // Launch threads
    for (size_t i = 0; i < thread_count; i++) {
        size_t start = i * elements_per_thread;
        size_t end   = (i == thread_count - 1) ? size : (start + elements_per_thread);
        threads[i]   = std::thread([&data, start, end, &barrier, size]() {
            // Perform bitonic sort
            for (size_t merge_size = 1; merge_size <= size; merge_size <<= 1) {
                for (size_t j = merge_size >> 1; j > 0; j >>= 1) {
                    for (size_t i = start; i < end; i++) {
                        bool   up = ((i / merge_size) % 2 == 0);
                        size_t k  = i ^ j;
                        if (k < size && (i & j) == 0 && (data[i] > data[k]) == up) {
                            std::swap(data[i], data[k]);
                        }
                    }
                    barrier.arrive_and_wait();
                }
            }
        });
    }

    // Join all threads
    for (auto &thread : threads) {
        thread.join();
    }
}

int64_t *random_array(size_t count) {
    int64_t *result = new int64_t[count];
    for (size_t i = 0; i < count; i++) {
        result[i] = rand();
    }
    return result;
}

std::string const USAGE = "Program requires exactly two arguments, both positive integers.\n";

void              bad_usage() {
    std::cerr << USAGE;
    std::exit(1);
}

void get_args(int argc, char *argv[], int &val_count, int &thread_count) {
    if (argc <= 2) {
        bad_usage();
    } else if (argc >= 4) {
        bad_usage();
    }

    val_count    = 0;
    thread_count = 0;

    val_count    = atoi(argv[1]);
    thread_count = atoi(argv[2]);

    if ((val_count <= 0) || (thread_count <= 0)) {
        bad_usage();
    }
}

void barrier_test_helper(size_t phase_count, size_t thread_id, size_t thread_count, Barrier *bar) {
    for (size_t i = 0; i < phase_count; i++) {
        uint8_t ms_sleep_count = rand() % 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep_count));
        std::cout << (char)('A' + thread_id);
        bar->arrive_and_wait();
        if (thread_id == 0) {
            std::cout << '\n';
        }
        bar->arrive_and_wait();
    }
}

void barrier_test() {
    std::cout << "Testing barrier functionality.\n\n";
    std::cout << "Each line should contain the same set of letters with no duplicates.\n\n";
    srand(time(nullptr));
    size_t      thread_count = 6;
    size_t      phase_count  = 10;
    std::thread team[thread_count];
    Barrier     bar(thread_count);

    for (size_t i = 0; i < thread_count; i++) {
        team[i] = std::thread(barrier_test_helper, phase_count, i, thread_count, &bar);
    }
    for (size_t i = 0; i < thread_count; i++) {
        team[i].join();
    }

    std::cout << "\n\n";
}

void bitonic_test() {
    std::cout << "Testing bitonic sort function.\n\n";
    size_t   magnitude = 20;
    size_t   size      = 1 << magnitude;
    int64_t *data      = random_array(size);
    int64_t *reference = new int64_t[size];
    for (size_t i = 0; i < size; i++) {
        reference[i] = data[i];
    }
    std::sort(reference, reference + size);

    parallel_bitonic_sort(data, size, 1);

    for (size_t i = 0; i < size; i++) {
        if (data[i] != reference[i]) {
            std::cout << "First output mismatch at index " << i << ".\n";
            return;
        }
    }
    std::cout << "All output values match!\n";

    delete[] reference; // Free memory
}

int main(int argc, char *argv[]) {
    int val_count, thread_count;
    get_args(argc, argv, val_count, thread_count);
    int64_t *data = random_array(val_count);

    // barrier_test();
    // bitonic_test();

    TimePoint start_time = steady_clock::now();
    parallel_bitonic_sort(data, val_count, thread_count);
    TimePoint end_time = steady_clock::now();
    TimeSpan  span     = duration_cast<TimeSpan>(end_time - start_time);

    std::cout << span.count();

    return 0;
}
