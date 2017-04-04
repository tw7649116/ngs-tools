#pragma once

#include "reader.h"
#include <thread>
#include <mutex>
#include <condition_variable>

template <typename ReaderType>
class MTReader final: public Reader {
private:
    typedef std::vector<Fragment> Chunk;
    struct Thread {
        // protected with global reader locked
        bool done;
        float progress;
        size_t next_chunk_idx;
        bool chunk_ready;
        Chunk loaded_chunk;
        std::condition_variable consumed;

        // private to thread
        size_t chunk_idx;
        ReaderType reader;
        std::thread thread_impl;

        template <typename ...Args>
        Thread(Args... args)
            : done(false)
            , progress(0)
            , chunk_ready(false)
            , chunk_idx(0)
            , reader(args...)
        {}
        Thread(Thread&& other) = delete;
        Thread(const Thread& other) = delete;

        void run(size_t chunk_size, size_t start_chunk_idx, std::mutex& mutex, std::condition_variable& ready) {
            try {
                run_impl(chunk_size, start_chunk_idx, mutex, ready);
            } catch (ngs::ErrorMsg error) {
                std::cerr << "Exception in reader thread: " << error.what() << std::endl;
                std::terminate();
            }
        }

        void run_impl(size_t chunk_size, size_t start_chunk_idx, std::mutex& mutex, std::condition_variable& ready) {
            size_t skip_to_idx = start_chunk_idx;
            Chunk chunk;
            std::unique_lock<std::mutex> lock(mutex);
            while (!done) {
                float chunk_progress = progress;
                lock.unlock();
                { // not locked section
                    assert(chunk_idx <= skip_to_idx);
                    size_t fragments_to_skip = (skip_to_idx - chunk_idx) * chunk_size;
                    for (size_t i = 0; i < fragments_to_skip; ++i) {
                        if (!reader.read(nullptr)) {
                            break;
                        }
                    }

                    chunk.resize(chunk_size);
                    for (size_t i = 0; i < chunk_size; ++i) {
                        if (!reader.read(&chunk[i])) {
                            chunk.resize(i);
                            break;
                        }
                    }
                    chunk_idx = skip_to_idx + 1;
                    
                    chunk_progress = reader.progress();
                }
                lock.lock();
                const bool reader_at_eof = chunk.size() < chunk_size;
                if (!chunk.empty()) {
                    while (chunk_ready) {
                        consumed.wait(lock);
                    }
                    std::swap(loaded_chunk, chunk);
                    chunk_ready = true;
                    skip_to_idx = next_chunk_idx;
                }
                progress = chunk_progress; 
                done = reader_at_eof;
                ready.notify_one();
            }
        }
    };
    const size_t chunk_size;
    bool all_done;
    size_t next_chunk_idx;
    std::vector<std::unique_ptr<Thread> > threads;
    mutable std::mutex mutex;
    std::condition_variable ready;
    
    Chunk current_chunk;
    size_t current_fragment_idx;

    
    bool load_chunk() {
        while (!all_done) {
            std::unique_lock<std::mutex> lock(mutex);
            size_t done_count = 0;
            for (auto& thread: threads) {
                if (thread->chunk_ready) {
                    current_fragment_idx = 0;
                    std::swap(current_chunk, thread->loaded_chunk);
                    thread->next_chunk_idx = next_chunk_idx;
                    ++next_chunk_idx;
                    thread->chunk_ready = false;
                    thread->consumed.notify_one();
                    return true;
                }
                if (thread->done) {
                    ++done_count;
                }
            }
            all_done = (done_count == threads.size());
            if (!all_done) {
                ready.wait(lock);
            }
        }
        return false;
    }
    
public:
    template <typename ...Args>
    MTReader(size_t thread_count, size_t chunk_size, Args... args)
        : chunk_size(chunk_size)
        , all_done(false)
        , current_fragment_idx(0)
    {
        assert(thread_count > 0);
        threads.resize(thread_count);

        #pragma omp parallel for
        for (size_t i = 0; i < thread_count; ++i) {
            threads[i] = std::unique_ptr<Thread>(new Thread(args...));
        }
        
        for (size_t i = 0; i < thread_count; ++i) {
            auto& thread = threads[i];
            const size_t start_chunk_idx = i;
            thread->next_chunk_idx = start_chunk_idx + thread_count;
            thread->thread_impl = std::thread(&MTReader::Thread::run, thread.get(), chunk_size, start_chunk_idx, std::ref(mutex), std::ref(ready));
        }
        next_chunk_idx = thread_count * 2;
    }
    ~MTReader() {
        std::unique_lock<std::mutex> lock(mutex);
        for (auto& thread: threads) {
            thread->done = true;
            thread->chunk_ready = false;
            thread->consumed.notify_one();
        }
        lock.unlock();
        for (auto& thread: threads) {
            thread->thread_impl.join();
        }
    }

    SourceStats stats() const override {
        assert(all_done);
        return threads[0]->reader.stats();
    }
    float progress() const override {
        std::unique_lock<std::mutex> lock(mutex);
        float max_progress = 0;
        for (auto& thread : threads) {
            max_progress = std::max(max_progress, thread->progress);
        }
        return max_progress;
    }

    bool read(Fragment* output) override {
        if (current_fragment_idx >= current_chunk.size()) {
            if (!load_chunk()) {
                return false;
            }
        }
        assert(current_fragment_idx < current_chunk.size());
        if (output) {
            std::swap(*output, current_chunk[current_fragment_idx]);
        }
        ++current_fragment_idx;
        return true;
    }

    bool read_many(std::vector<Fragment>& output) override {
        if (current_fragment_idx >= current_chunk.size()) {
            if (!load_chunk()) {
                output.clear();
                return false;
            }
        }
        if (current_fragment_idx == 0) {
            std::swap(current_chunk, output);
            current_fragment_idx = current_chunk.size();
        } else {
            size_t count = current_chunk.size() - current_fragment_idx;
            output.resize(count);
            for (size_t i = 0; i < count; ++i) {
                std::swap(output[i], current_chunk[i + current_fragment_idx]);
            }
            current_fragment_idx += count;
        }
        assert(!output.empty());
        return true;
    }
};
