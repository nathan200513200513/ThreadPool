#include "../include/threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLETIME = 60;  //线程最大空闲时间，单位秒
/////////////////////////////////////////////////////////////////线程池方法
//线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , curThreadSize_(0)
    , taskSize_(0)
    , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
    , idleThreadSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , PoolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
{}

//线程池析构
ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;

    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]()-> bool { return threads_.size() == 0; });
}

//检查线程运行状态
bool ThreadPool::checkRunningState() const
 {
    return isPoolRunning_;
}

//设置线程池工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    PoolMode_ = mode;
}

//设置任务队列上限
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
    if (checkRunningState())
        return;
    taskQueMaxThreshHold_ = threshhold;
}

//设置线程的数量上限
void ThreadPool::setThreadThreshHold(int threshhold)
{
    if (checkRunningState())
        return;
    //cached模式下才设置线程数量上限，fixed模式下没必要
    if (PoolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshHold_ = threshhold;
    }
}

//提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) 
{
    //获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    //线程通信，等待任务队列空余
    // while (threads_.size() == taskQueMaxThreshHold_)
    // {
    //     notFull_.wait(lock);
    // }
    //用户提交任务，最长不能阻塞超过一秒，否则判断任务提交失败，返回
     if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
        [&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
        {
            //表示等待了一秒后任务队列还是没有空余，直接返回
            std::cerr << "task queue is full, submit task is fail!" << std::endl;
            // return Result(sp, false); 
            return Result(sp, false);
        }
    //如果任务队列有空余，放入任务
    taskQue_.emplace(sp);
    taskSize_++;
    //在not_Empty上通知
    notEmpty_.notify_all();
    //返回任务的result对象
    // return Result(sp);

    //cached模式下，判断需不需要增加线程数量
    if (PoolMode_ == PoolMode::MODE_CACHED 
        && taskSize_ > idleThreadSize_ 
        && curThreadSize_ < threadSizeThreshHold_)
    {
        std::cout << "new thread create" << std::endl; 

        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        //启动线程
        threads_[threadId]->start();
        //修改线程数量相关变量
        curThreadSize_++;
        idleThreadSize_++;
    }

    return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize)
{

    isPoolRunning_ = true;

    //线程初始数量
    initThreadSize_ = initThreadSize; 
    curThreadSize_ = initThreadSize; 
    //创建线程对象
    for (int i = 0; i < initThreadSize_; ++i)
    {
        //创建线程对象时需要将线程函数传递给他
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    //启动线程
    for (int i = 0; i < initThreadSize_; ++i)
    {
        threads_[i]->start();
        //空闲的线程数量增加
        idleThreadSize_++;
    }
}

//定义线程函数
void ThreadPool::threadFunc(int threadId)

{
    // std::cout << "begin threadFunc threadid:" << std::this_thread::get_id() << std::endl;
    // std::cout << "end threadFunc threadid:" << std::this_thread::get_id() << std::endl;
    
    //记录每个线程刚开始执行时的时间
    auto lastTime = std::chrono::high_resolution_clock().now();

    // //对线程运行状态的检查
    // if (!checkRunningState())
    // {
    //     std::cout << "thread" << std::this_thread::get_id() << "exit due to pool shutdown" << std::endl;
    //     return;
    // }


    for (;;)
    {
        std::shared_ptr<Task> task;
        
        //这个作用域使线程拿到任务后就释放锁
        {  
            //先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            
            //测试输出
            std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务" << std::endl;
            
            //cached模式下，对于超过initThreadSize_数量多线程，空闲时间超过60秒的线程应该回收
            //当前时间 - 上一次线程执行任务的时间 > 60s
            while(taskQue_.size() == 0) //锁加双重判断防止死锁 
            {
                if (!isPoolRunning_)
                {
                    threads_.erase(threadId);
                    std::cout << "thread" << std::this_thread::get_id() << "exit" << std::endl;
                    exitCond_.notify_all();
                }   

                if (PoolMode_ == PoolMode::MODE_CACHED)
                {
                    //条件变量wait_for有两个返回值类型，no_timeout和timeout,分别表示非超时返回和超时返回
                    //每一秒返回一次，区分超时返回还是有任务待执行返回
                    if (std::cv_status::timeout == 
                        notEmpty_.wait_for(lock, std::chrono::seconds(1)))  //等待了一秒之后如果任务队列还是空的
                    {
                        //当前时间
                        auto now = std::chrono::high_resolution_clock().now();
                        //当前时间和上一次线程执行任务时间的差值
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLETIME
                            && curThreadSize_ > initThreadSize_)
                        {
                            //回收当前线程
                            threads_.erase(threadId);
                            std::cout << "thread" << std::this_thread::get_id() << "exit" << std::endl;
                            curThreadSize_--;
                            idleThreadSize_--;
                            return;
                        }
                    }
                }
                else
                {
                    //等待not_Empty
                    notEmpty_.wait(lock);
                }

                // //等待在notEmpty条件变量上的线程被线程池的析构函数唤醒以后，回收
                // if (!isPoolRunning_)
                // {
                //     threads_.erase(threadId);
                //     std::cout << "thread" << std::this_thread::get_id() << "exit" << std::endl;
                //     exitCond_.notify_all();
                // }
            }
            if (!isPoolRunning_)
            { 
                break;
            }

            idleThreadSize_--;
            
            //测试输出
            std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功" << std::endl;
            
            //从任务队列获取一个任务
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;
            
            //如果还有任务，通知其他线程
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }
            
            //通知可以继续提交任务
            notFull_.notify_all();
        }

        //当前线程执行该任务
        if (task != nullptr)
            task->exec();
        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock().now();
    }

    //正在执行任务的线程发现线程池将停止运行，回收
    // threads_.erase(threadId);
    // std::cout << "thread" << std::this_thread::get_id() << "exit" << std::endl;
    // exitCond_.notify_all();
}

/////////////////////////////////////////////////////////////////Task方法
Task::Task()
    : result_(nullptr)
{}


//封装用户写的run方法
void Task::exec()
{
    if (result_ != nullptr)
    {
        result_->setVal(run());
    }
}

void Task::setResult(Result* res)
{
    this->result_ = res; 
}
/////////////////////////////////////////////////////////////////Result方法
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task)
    , isValid_(isValid)
{
    task_->setResult(this);
}

Result::Result(Result&& other) noexcept
    : any_(std::move(other.any_))
    // , task_(std::move(other.task_))
    // , sem_(std::move(other.sem_))
{}

Any Result::get()
{
    if (!isValid_)
    {
        return "";
    }
    sem_.wait();
    return std::move(any_);
}

void Result::setVal(Any any)
{
    //储存task返回值
    this->any_ = std::move(any);
    sem_.post();
}

/////////////////////////////////////////////线程方法
//线程构造
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
{}

//线程析构 
Thread::~Thread()
{}

int Thread::generateId_ = 0;

int Thread::getId() const
{
    return threadId_;
}

//开启线程
void Thread::start()
{
    //创建一个线程来执行一个线程函数
    std::thread t(func_, threadId_);
    t.detach();
}