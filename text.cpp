#include "../include/threadpool.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

using ULong = unsigned long long;

class myTask : public Task
{
    public:
        myTask(int begin, int end)
            : begin_(begin)
            , end_(end)
        {}

        Any run()
        {
            std::cout << "tid" << std::this_thread::get_id() << "begin" << std::endl;
            ULong sum = 0;
            for (ULong i = begin_; i < end_; i++)
            {
                sum += i;
            }
            std::cout << "tid" << std::this_thread::get_id() << "end" << std::endl;
            return sum;
        }
    private:
        int begin_;
        int end_;
};

int main()
{
    ThreadPool pool;
    pool.start();
    Result res1 = pool.submitTask(std::make_shared<myTask>(1, 100000001));
    Result res2 = pool.submitTask(std::make_shared<myTask>(100000001, 200000001));
    Result res3 = pool.submitTask(std::make_shared<myTask>(200000001, 300000000));
    ULong sum1 = res1.get().cast<ULong>();
    ULong sum2 = res2.get().cast<ULong>();
    ULong sum3 = res3.get().cast<ULong>();

    std::cout << (sum1 + sum2 + sum3) << std::endl;
    // pool.submitTask(std::make_shared<myTask>());
    // pool.submitTask(std::make_shared<myTask>());
    // pool.submitTask(std::make_shared<myTask>());
    // pool.submitTask(std::make_shared<myTask>());
    // pool.submitTask(std::make_shared<myTask>());
    // pool.submitTask(std::make_shared<myTask>());
    std::cin.get();
    return 0;
}