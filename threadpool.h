#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

//线程池运行模式
enum class PoolMode
{
    MODE_FIXED, //固定线程模式
    MODE_CACHED //动态增长线程模式
};

//接收任意类型的Any类
class Any
{
    public:
        Any() = default;
        ~Any() = default;
        Any(const Any&) = delete;
        Any& operator=(const Any&) = delete;
        Any(Any&& other) noexcept : base_(std::move(other.base_)) {};
        Any& operator=(Any&& other) noexcept
        {
            if (this != &other)
            {
                base_ = std::move(other.base_);
            }
            return *this;
        }
        //用于接收任意类型的参数
        template <typename T>
        Any(T Data) : base_(std::make_unique<Drive<T>>(Data))
        {} 
        
        //Result res = pool.submitTask()
        //res.get().cast<..>()
        //获取接收的返回值
        template <typename T>
        T cast()
        {
            //将基类指针转换为派生类指针
            Drive<T> *pd = dynamic_cast<Drive<T>*>(base_.get());
            if (pd == nullptr)
            {
                throw "type is unmatch!";
            }
            return pd->data_;
        }
    private:
        class Base
            {
                public:
                    virtual ~Base() = default;
            };

        template <typename T>
        class Drive : public Base
        {
            public:
                Drive(T Data)
                    : data_(Data)
                {}
                T data_;
        };
    private:
        std::unique_ptr<Base> base_;
};

//实现信号量类
class Semaphore
{
    public:
        Semaphore(int limit = 0)
            : resLimit_(limit)
        {}
        ~Semaphore() {}

        //消耗一个信号量
        void wait()
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
            resLimit_--;
        }

        //提交一个信号量
        void post()
        {
            std::unique_lock<std::mutex> lock(mtx_);
            resLimit_++;
            cond_.notify_all();
        }
    private:
        int resLimit_;
        std::mutex mtx_;
        std::condition_variable cond_;
};

class Task;

//实现接收submitTask函数执行完的返回值
class Result
{
    public:
        Result(std::shared_ptr<Task> task, bool isValid = true);
        ~Result() = default;

        //setVal方法
        void setVal(Any any);
        //get方法
        Any get();
    private:
        Any any_;
        Semaphore sem_;
        std::shared_ptr<Task> task_;
        std::atomic_bool isValid_;
};

//任务基类
class Task
{
    public:
        Task();
        //Task(Result* res);
        ~Task() = default;

        virtual Any run() = 0;

        void setResult(Result* res);

        void exec();
    private:
        Result* res_;      
};

//线程类
class Thread
{
    public:
        using ThreadFunc = std::function<void()>;

        Thread(ThreadFunc func);

        ~Thread();

        //启动线程
        void start();

    private:
        ThreadFunc func_;
};

//线程池类
class ThreadPool
{
    public:
        //线程构造
        ThreadPool();

        //线程析构
        ~ThreadPool();

        //设置线程模式
        void setPoolMode(PoolMode mode);

        //设置任务数量上限
        void setTaskMaxQueThreshHold(int threshhold);

        //用户提交任务
        Result submitTask(std::shared_ptr<Task> sp);

        //启动线程池
        void start(int initThreadSize = 4);
        
        //禁用拷贝构造函数和赋值重载函数
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
    private:
        //线程函数
        void threadFunc();
    private:
        std::vector<std::unique_ptr<Thread>> threads_; //线程列表
        int initThreadSize_; //初始线程数量

         std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
        std::atomic_int taskSize_; //任务数量
        int taskQueMaxThreshHold_; //任务数量上限

        std::mutex taskQueMtx_; //确保任务队列线程安全
        std::condition_variable notFull_; //表示任务队列未满
        std::condition_variable notEmpty_; //表示任务队列不空

        PoolMode poolMode_; //当前线程池的运行模式

};