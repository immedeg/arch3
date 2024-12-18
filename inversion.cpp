#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>


struct Task {
    unsigned char* data;
    int width;
    int height;
    int channels;
};

class BlockingQueue {
public:
    void push(const Task& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(item);
        lock.unlock();
        condition_.notify_one();
    }

    Task pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !queue_.empty(); });
        Task item = queue_.front();
        queue_.pop();
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue<Task> queue_;
    std::condition_variable condition_;
};

// Обработка задач
class Consumer {
public:
    Consumer(int num_threads) : done_(false) {
        for (int i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() { this->worker(); });
        }
    }

    ~Consumer() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;  // Сигнализируем потокам о завершении
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }

    void enqueue(const Task& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        condition_.notify_one();
    }

private:
    void worker() {
        while (true) {
            Task task;
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return done_ || !tasks_.empty(); });
            if (done_ && tasks_.empty()) {
                return; 
            }
            task = tasks_.pop();
            process(task);
        }
    }

    void process(Task task) {
        for (int i = 0; i < task.width * task.height * task.channels; ++i) {
            task.data[i] = 255 - task.data[i]; // Инверсируем цвета
        }
    }

    std::vector<std::thread> workers_;
    BlockingQueue tasks_;
    std::atomic<bool> done_;
    std::mutex mutex_;  // Защита для доступа к done_ и tasks_
    std::condition_variable condition_;
};

// Producer для создания задач
void producer(Consumer& consumer, unsigned char* image, int width, int height, int channels, int num_tasks) {
    int rows_per_task = height / num_tasks;

    for (int i = 0; i < num_tasks; ++i) {
        Task task;
        task.data = image + i * rows_per_task * width * channels;
        task.width = width;
        task.height = (i == num_tasks - 1) ? (height - i * rows_per_task) : rows_per_task;
        task.channels = channels;
        consumer.enqueue(task);
    }
}


int main() {
    int width, height, channels;
    
    unsigned char* image = stbi_load("/Users/yusha/Documents/VSCode/archit/lab#3/input1.jpg", &width, &height, &channels, 0);
    if (!image) {
        std::cerr << "Could not open or find the image\n";
        return 1;
    }

    Consumer consumer(std::thread::hardware_concurrency());

    const int num_tasks = 8; // Количество задач
    producer(consumer, image, width, height, channels, num_tasks);

    // Дожидаемся завершения потоков, работающих в Consumer

    if (!stbi_write_png("/Users/yusha/Documents/VSCode/archit/lab#3/output.png", width, height, channels, image, width * channels)) {
        std::cerr << "Failed to save the output image\n";
        stbi_image_free(image);
        return 1;
    }

    stbi_image_free(image);
    std::cout << "Image inverted and saved as output.png\n";
    return 0;
}