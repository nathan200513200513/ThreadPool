#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>

//线程模式
enum class PoolMode
{
    MODE_FIXED, //线程数量固定
    MODE_CACHED, //线程数量可动态增长
};

//semaphore信号量类
class Semaphore
{
    public:
        Semaphore(int limit = 0)
            : resLimit_(limit)
        {}
        ~Semaphore() = default;

        // Semaphore(Semaphore&& other) noexcept = default;
        // Semaphore& operator=(Semaphore&& other) noexcept = default;

        void wait()
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
            resLimit_--;
        }
        void post()
        {
            std::unique_lock<std::mutex> lock(mtx_);
            resLimit_++;
            cond_.notify_all();
        }
    private:
        std::mutex mtx_;
        std::condition_variable cond_;
        int resLimit_;
};

//any类，可以接受任意数据的类型,利用基类指针可以指向派生类对象
class Any
{
    public:
        Any() = default;
        ~Any() = default;
        //unique_ptr没有左值引用，只有右值引用
        Any(const Any&) = delete;
        Any& operator=(const Any&) = delete;
        Any(Any&&) = default;
        Any& operator=(Any&&) = default;
        //这个构造函数用来接收任意类型的数据
        template<typename T>
        //Any(T data) : base_(new Derive<T>(data))
        //    {}
        Any(T data) : base_(std::make_unique<Derive<T>>(data))
            {} 
        //
        template<typename T>
        T cast_()
        {
            //将基类指针转换为派生类指针
            Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
            if (pd == nullptr)
            {
                throw "type is unmatch!";
            }
            return pd->data_;
        }
    private:
        //基类
        class Base
        {
            public:
                virtual ~Base() = default;
        };
        //派生类
        template<typename T>
        class Derive : public Base
        {
            public:
                Derive(T data) : data_(data)
                    {}
                T data_;
        };
    private:
        std::unique_ptr<Base> base_;

};

//前置声明class类
class Task;

//Result类，用于接收用户调用提交任务方法submitTask产生的返回值
class Result
{
    public:
        Result(std::shared_ptr<Task> task, bool isValid = true);
        ~Result() = default;

        Result(Result&& other) noexcept;

        //setval方法，获取任务执行完后的返回值
        void setVal(Any any);
        //get方法，用户调用获取task的返回值
        Any get();

    private:
        Any any_;  //储存任务的返回值
        std::shared_ptr<Task> task_;
        Semaphore sem_;
        std::atomic_bool isValid_;  //如果任务提交失败，则该值为false
};

//任务基类
class Task
{
    public:
        Task();
        ~Task() = default; 
        void exec();
        void setResult(Result* res);

        //用户自定义任意任务类型，继承自Task类，重写run方法，实现自定义任务处理
        virtual Any run() = 0;
    private:
        Result* result_;
};


//线程类
class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;

    //线程构造
    Thread(ThreadFunc func);
    //线程析构
    ~Thread();
    
    int getId() const;
    void start();
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; //保存线程id
};

//线程池类
class ThreadPool
{
public:
    ThreadPool();

    ~ThreadPool();

    //设置线程池工作模式
    void setMode(PoolMode mode);

    //设置任务队列上限
    void setTaskQueMaxThreshHold(int threshhold);

    //设置线程数量上限
    void setThreadThreshHold(int threshhold);
    
    //提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    //开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());  //线程初始值数量为cpu核心数量

    //禁止对线程池进行拷贝构造、赋值构造
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void threadFunc(int threadId); //线程函数

    //检查线程的运行状态
    bool checkRunningState() const; 

private:
    // std::vector<std::unique_ptr<Thread>> threads_;  //线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;
    size_t initThreadSize_; //线程初始大小
    int threadSizeThreshHold_; //线程的数量上限
    std::atomic_int idleThreadSize_; //空闲的线程的数量
    std::atomic_int curThreadSize_; //当前线程的数量

    std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
    std::atomic_uint taskSize_; //任务数量
    int taskQueMaxThreshHold_; //任务队列数量上限 

    std::mutex taskQueMtx_; //确保任务队列线程安全
    std::condition_variable notFull_; //任务队列不满
    std::condition_variable notEmpty_; //任务队列不空

    PoolMode PoolMode_; //当前线程池的工作模式
    std::atomic_bool isPoolRunning_;  //线程运行状态

    std::condition_variable exitCond_; //线程池存在
};

#endif
