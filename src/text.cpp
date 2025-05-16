#include <iostream>
#include <chrono>
#include <thread>
#include "../include/threadpool.h"

using uLong = unsigned long long;
class Mytask : public Task
{
    public:
        Mytask(int begin, int end)
            : begin_(begin)
            , end_(end)
        {}
        Any run()
        {
            std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
            // std::this_thread::sleep_for(std::chrono::seconds(3));
            uLong sum = 0;
            for (uLong i = begin_; i < end_; ++i)
            {
                sum += i;
            }
            std::cout << "tid" << std::this_thread::get_id() << "end!" << std::endl;
            return sum;
        }
    private:
        int begin_;
        int end_;
};

int main()
{
    
    ThreadPool pool;

    pool.setMode(PoolMode::MODE_CACHED);

    pool.start(2);
    Result res1 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    Result res2 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    Result res3 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    Result res4 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    uLong sum1 = res1.get().cast_<uLong>();
    std::cout << sum1 << std::endl;
    
    std::cout << "main over!" << std::endl;
    
    // ThreadPool pool;

    //用户自己定义线程池模式
    // pool.setMode(PoolMode::MODE_CACHED);
     
    // pool.start(4);
    // std::shared_ptr<Result> res1 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    // std::shared_ptr<Result> res2 = pool.submitTask(std::make_shared<Mytask>(100000001, 200000000));
    // std::shared_ptr<Result> res3 = pool.submitTask(std::make_shared<Mytask>(200000001, 300000000));
    // std::shared_ptr<Result> res4 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));

    // std::shared_ptr<Result> res5 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));
    // std::shared_ptr<Result> res6 = pool.submitTask(std::make_shared<Mytask>(1, 100000000));

    // uLong sum1 = res1->get().cast_<uLong>();
    // uLong sum2 = res2->get().cast_<uLong>();
    // uLong sum3 = res3->get().cast_<uLong>();
    // std::cout << sum1 + sum2 + sum3 << std::endl;
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    // pool.submitTask(std::make_shared<Mytask>());
    std::cin.get();
    return 0;
}