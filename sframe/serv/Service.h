﻿
#ifndef SFRAME_SERVICE_H
#define SFRAME_SERVICE_H

#include <assert.h>
#include <vector>
#include <memory>
#include "../util/Lock.h"
#include "MessageDecoder.h"
#include "../util/Delegate.h"
#include "../util/Singleton.h"
#include "ServiceDispatcher.h"

namespace sframe{

// 服务状态枚举
enum ServiceState : int32_t
{
	kServiceState_Idle = 1,       // 闲置
	kServiceState_WaitProcess,    // 等待处理
	kServiceState_Processing,     // 处理中
};

class Service;

// 服务消息队列
class MessageQueue : public noncopyable
{
public:
	MessageQueue(Service * service) : _related_service(service), _state(kServiceState_Idle)
	{
		assert(_related_service);
		_buf_write = new std::vector<std::shared_ptr<Message>>();
		_buf_read = new std::vector<std::shared_ptr<Message>>();
		_buf_write->reserve(1024);
		_buf_read->reserve(1024);
	}

	void Push(const std::shared_ptr<Message> & msg);

	std::vector<std::shared_ptr<Message>> * PopAll();

	void EndProcess();

private:
	Service * _related_service;
	SpinLock _lock;
	std::vector<std::shared_ptr<Message>> * _buf_write;
	std::vector<std::shared_ptr<Message>> * _buf_read;
	ServiceState _state;
};

// 工作服务
class Service : public noncopyable
{
public:
	static const int32_t kDefaultMaxWaitDestroyTime = 100000;

public:
	// 初始化（创建服务成功后调用，此时还未开始运行）
	virtual void Init() = 0;

	// 处理周期定时器
	virtual void OnCycleTimer() {}

	// 处理开始（此时已经开始运行）
	virtual void OnStart() {}

	// 处理销毁
	virtual void OnDestroy() {}

	// 服务接入
	virtual void OnServiceJoin(const std::unordered_set<int32_t> & sid_set, bool is_remote) {}

	// 是否销毁完成
	virtual bool IsDestroyCompleted() const
	{
		return true;
	}

	// 是否关心指定服务的接入
	virtual bool IsCareServiceJoin(int32_t sid) const
	{
		return false;
	}

	// 获取服务的循环定时器周期，重写此方法返回大于0的值(ms)设置循环周期
	virtual int32_t GetCyclePeriod() const
	{
		return 0;
	}

public:
    Service() : _sid(0), _msg_queue(this), _last_sid(0), _cur_time(0) {}

    virtual ~Service() {}

	void SetServiceId(int32_t sid)
	{
		_sid = sid;
	}

	int32_t GetServiceId() const
	{
		return _sid;
	}

    // 压入消息
	void PushMsg(const std::shared_ptr<Message> & msg)
	{
		_msg_queue.Push(msg);
	}

    // 处理
    void Process();

	// 等待销毁完毕
	void WaitDestroyComplete();

	// 获取最近发送消息给本服务的服务ID
	int32_t GetLastServiceId() const
	{
		return _last_sid;
	}

	// 获取当前时间
	int64_t GetTime() const
	{
		return _cur_time;
	}

	// 注册服务消息处理函数(内部服务消息和网络服务消息都注册)
	template<typename Func_Type, typename Obj_Type>
	bool RegistServiceMessageHandler(int msg_id, Func_Type func, Obj_Type * obj)
	{
		bool ret = false;
		ret = _inside_delegate_mgr.Regist(msg_id, func, obj);
		ret = _net_delegate_mgr.Regist(msg_id, func, obj) && ret;
		return ret;
	}

	// 注册内部服务消息处理函数
	template<typename Func_Type, typename Obj_Type>
	bool RegistInsideServiceMessageHandler(int msg_id, Func_Type func, Obj_Type * obj)
	{
		return _inside_delegate_mgr.Regist(msg_id, func, obj);
	}

	// 注册网络服务消息处理函数
	template<typename Func_Type, typename Obj_Type>
	bool RegistNetServiceMessageHandler(int msg_id, Func_Type func, Obj_Type * obj)
	{
		return _net_delegate_mgr.Regist(msg_id, func, obj);
	}

	// 发送内部服务消息
	template<typename... T_Args>
	void SendInsideServiceMsg(int32_t dest_sid, uint16_t msg_id, T_Args&... args)
	{
		ServiceDispatcher::Instance().SendInsideServiceMsg(_sid, dest_sid, msg_id, args...);
	}

	// 发送网络服务消息
	template<typename... T_Args>
	void SendNetServiceMsg(int32_t dest_sid, uint16_t msg_id, T_Args&... args)
	{
		ServiceDispatcher::Instance().SendNetServiceMsg(_sid, dest_sid, msg_id, args...);
	}

	// 发送服务消息
	template<typename... T_Args>
	void SendServiceMsg(int32_t dest_sid, uint16_t msg_id, T_Args&... args)
	{
		ServiceDispatcher::Instance().SendServiceMsg(_sid, dest_sid, msg_id, args...);
	}

private:
	int32_t _sid;
	int64_t _cur_time;               // 当前时间
	MessageQueue _msg_queue;         // 消息队列
	int32_t _last_sid;               // 最近收到的服务消息的服务ID
	DelegateManager<InsideServiceMessageDecoder, 65536> _inside_delegate_mgr;
	DelegateManager<NetServiceMessageDecoder, 65536> _net_delegate_mgr;
};

}

#endif