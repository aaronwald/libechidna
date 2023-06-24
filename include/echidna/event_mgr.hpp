#pragma once

#include <iostream>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <memory>
#include <functional>
#include <deque>
#include <set>
#include <sys/epoll.h>

#include "event_hlpr.hpp"
#include "mem.hpp"

namespace coypu::event
{
  typedef std::function<int(int)> callback_type;
  template <typename LogTrait>
  class EventManager
  {
  public:
    EventManager(LogTrait logger) : _emptyCB(nullptr), _growSize(8),
                                    _fdToCB(_growSize, _emptyCB),
                                    _fdEvents(_growSize, 0),
                                    _fd(0), _logger(logger),
                                    _timeout(1000), _maxEvents(16), _outEvents(nullptr)
    {
      _outEvents = reinterpret_cast<struct epoll_event *>(malloc(sizeof(struct epoll_event) * _maxEvents));
    }

    virtual ~EventManager()
    {
      if (_outEvents)
      {
        delete _outEvents;
        _outEvents = nullptr;
      }
    }

    int Init()
    {
      _fd = coypu::event::EPollHelper::Create();
      if (_fd < 0)
      {
        if (_logger)
        {
          perror(errno, "Failed to create epoll");
        }
        return -1;
      }
      return 0;
    }

    int Close()
    {
      if (_fd > 0)
      {
        ::close(_fd);
      }

      return 0;
    }

    int Register(int fd, callback_type read_func, callback_type write_func, callback_type close_func)
    {
      if (fd <= 0)
      {
        assert(false);
        return -1;
      }
      struct epoll_event event;
      event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI; // always | EPOLLERR | EPOLLHUP;

      auto cb = std::make_shared<event_cb_type>();
      cb->_cf = close_func;
      cb->_wf = write_func;
      cb->_rf = read_func;
      cb->_fd = fd;
      while (_fdToCB.size() < static_cast<size_t>(fd + 1))
      {
        _fdToCB.resize(_fdToCB.size() + _growSize, _emptyCB);
        _fdEvents.resize(_fdEvents.size() + _growSize, 0);
      }

      assert(static_cast<size_t>(fd) < _fdToCB.capacity());
      assert(_fdToCB[fd].get() == nullptr);
      _fdToCB[fd] = cb;
      _fdEvents[fd] = 0; // reset

      event.data.fd = fd;

      int r = EPollHelper::Add(_fd, fd, &event);
      if (r != 0)
      {
        assert(false);
        Unregister(fd); // cleanup
      }
      return r;
    }

    int SetWrite(int fd)
    {
      struct epoll_event event;
      event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI; // always | EPOLLERR | EPOLLHUP;
      event.data.fd = fd;
      return EPollHelper::Modify(_fd, fd, &event);
    }

    int ClearWrite(int fd)
    {
      struct epoll_event event;
      event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI; // always | EPOLLERR | EPOLLHUP;
      event.data.fd = fd;
      return EPollHelper::Modify(_fd, fd, &event);
    }

    int Unregister(int fd)
    {
      int r = EPollHelper::Delete(_fd, fd);
      _fdToCB[fd].reset();

      return r;
    }

    uint64_t GetEventCount(int fd)
    {
      assert(fd >= 0);
      assert(fd < _fdEvents.size());
      return _fdEvents[fd];
    }

    int Wait()
    {
      int count = ::epoll_wait(_fd, _outEvents, _maxEvents, _timeout);
      if (count > 0)
      {
        if (count == _maxEvents)
        {
          if (_logger)
          {
            _logger->warn("Hit epoll _maxEvents [{0}].", _maxEvents);
          }
        }
        for (int i = 0; i < count; ++i)
        {
          std::shared_ptr<event_cb_type> &cb = _fdToCB[_outEvents[i].data.fd];
          assert(cb);
          ++_fdEvents[_outEvents[i].data.fd];

          if (_outEvents[i].events & (EPOLLIN | EPOLLPRI))
          {
            if (cb->_rf)
            {
              // ret < 0 : close
              int ret = cb->_rf(cb->_fd);
              if (ret < 0)
              {
                _closeSet.insert(cb->_fd);
              }
            }
          }

          if (_outEvents[i].events & EPOLLOUT)
          {
            if (cb->_wf)
            {
              // ret < 0 : close
              // ret 0 : clear
              // ret > 0 : keep EPOLLOUT bit set
              int ret = cb->_wf(cb->_fd);
              if (ret < 0)
              {
                _closeSet.insert(cb->_fd);
              }
              if (ret == 0)
              {
                ClearWrite(cb->_fd);
              }
            }
          }

          if (_outEvents[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
          {
            _closeSet.insert(cb->_fd);
            ::close(cb->_fd);
          }
        }

        if (!_closeSet.empty())
        {
          auto e = _closeSet.end();
          for (auto b = _closeSet.begin(); b != e; ++b)
          {
            std::shared_ptr<event_cb_type> &cb = _fdToCB[*b];
            assert(cb);

            if (cb->_cf)
            {
              cb->_cf(cb->_fd);
              cb->_cf = nullptr; // fire once
            }

            Unregister(*b);
          }
        }

        _closeSet.clear();
      }
      else if (count < 0)
      {
        if (errno == EINTR)
        {
          if (_logger)
          {
            perror(errno, "epoll_wait");
          }
          return 0;
        }
        else
        {
          if (_logger)
          {
            perror(errno, "epoll_wait");
          }
        }
      }
      return count;
    }

  private:
    EventManager(const EventManager &other) = delete;
    EventManager &operator=(const EventManager &other) = delete;
    EventManager(const EventManager &&other) = delete;
    EventManager &operator=(const EventManager &&other) = delete;

    void perror(int errnum, const char *msg)
    {
      char buf[1024] = {};
      _logger->error("[{0}] ({1}): {2}", errnum, strerror_r(errnum, buf, 1024), msg);
    }

    typedef struct EventCB
    {
      callback_type _rf;
      callback_type _wf;
      callback_type _cf;
      int _fd;
    } event_cb_type;

    std::shared_ptr<event_cb_type> _emptyCB;
    uint32_t _growSize;
    std::vector<std::shared_ptr<event_cb_type>> _fdToCB;
    std::vector<uint64_t> _fdEvents;

    int _fd;
    LogTrait _logger;

    int _timeout;
    int _maxEvents;
    struct epoll_event *_outEvents;
    std::set<int> _closeSet;
  };

  template <typename CBType>
  class EventCBManager
  {
  public:
    typedef std::function<int(int)> write_cb_type;
    typedef std::deque<uint64_t> queue_type;

    // fd should be eventfd()
    EventCBManager(int fd, write_cb_type set_write) : _fd(fd), _set_write(set_write)
    {
    }

    virtual ~EventCBManager()
    {
    }

    bool Register(uint64_t type, CBType &cb)
    {
      return _cbMap.insert(std::make_pair(type, cb)).second;
    }

    bool Unregister(uint64_t type)
    {
      auto b = _cbMap.find(type);
      if (b != _cbMap.end())
      {
        _cbMap.erase(b);
        return true;
      }
      return false;
    }

    int Read(int fd [[maybe_unused]])
    {
      uint64_t u = UINT64_MAX;
      // ignore the value
      int r = ::read(_fd, &u, sizeof(uint64_t));
      if (r > 0)
      {
        assert(r == sizeof(uint64_t));
        if (static_cast<size_t>(r) < sizeof(uint64_t))
          return -128;

        while (!_queue.empty())
        {
          u = _queue.front(); // not thread safe
          _queue.pop_front();
          auto b = _cbMap.find(u);
          if (b != _cbMap.end())
          {
            (*b).second();
          }
          else
          {
            assert(false);
          }
        }
        return 0;
      }
      return r;
    }

    // With io uring the read is already accomplished in the buffer
    int ReadIO(int fd [[maybe_unused]], int res [[maybe_unused]], int flags [[maybe_unused]])
    {
      uint64_t u = UINT64_MAX;

      if (res == sizeof(uint64_t))
      {
        while (!_queue.empty())
        {
          u = _queue.front(); // not thread safe
          _queue.pop_front();
          auto b = _cbMap.find(u);
          if (b != _cbMap.end())
          {
            (*b).second();
          }
          else
          {
            assert(false);
          }
        }
        return 0;
      }

      return -1;
    }

    int Write(int fd [[maybe_unused]])
    {
      // write queue
      uint64_t u = _queue.size();
      int r = ::write(_fd, &u, sizeof(uint64_t));

      if (r > 0)
      {
        assert(r == sizeof(uint64_t));
        if (static_cast<size_t>(r) < sizeof(uint64_t))
          return -128;
        return 0;
      }
      assert(false);

      return -1;
    }

    int QueueSend(coypu_io_uring &ring, uint64_t u)
    {
      _queue.push_back(u);
      u = _queue.size();
      struct IOCallback cb_recv(_fd, IORING_OP_SEND);

      return IOURingHelper::SubmitSend(ring,
                                       _fd,
                                       reinterpret_cast<char *>(u),
                                       sizeof(uint64_t),
                                       *reinterpret_cast<uint64_t *>(&cb_recv));
    }

    int Close(int fd [[maybe_unused]])
    {
      // nop ?? error
      return -1;
    }

    void Queue(uint64_t u)
    {
      _queue.push_back(u);
      _set_write(_fd);
    }

  private:
    EventCBManager(const EventCBManager &other) = delete;
    EventCBManager &operator=(const EventCBManager &other) = delete;
    EventCBManager(const EventCBManager &&other) = delete;
    EventCBManager &operator=(const EventCBManager &&other) = delete;

    queue_type _queue;

    typedef std::unordered_map<uint64_t, CBType> cb_map_type;
    cb_map_type _cbMap;

    int _fd;
    write_cb_type _set_write;
  };

  struct IOCallback
  {
    int _fd;
    uint8_t _cb;
    char _pad[3];

    IOCallback(int fd, uint8_t cb) : _fd(fd), _cb(cb)
    {
      _pad[0] = _pad[1] = _pad[2] = 0;
    }
  } __attribute__((__packed__));

  class IOCallbacks
  {
  public:
    typedef std::function<void(int, int, int)> cb_func_t; // fd, res, userdata

    IOCallbacks()
    {
      _cbs.resize(IORING_OP_LAST);
    }

    virtual ~IOCallbacks() {}

    void SetCallback(io_uring_op cb, cb_func_t &f)
    {
      _cbs[cb] = f;
    }

    void Fire(io_uring_op cb, int fd, int res, int flags)
    {
      if (_cbs[cb])
      {
        _cbs[cb](fd, res, flags);
      }
    }

  private:
    IOCallbacks(const IOCallbacks &) = delete;
    IOCallbacks &operator=(const IOCallbacks &) = delete;
    IOCallbacks(IOCallbacks &&) = delete;
    IOCallbacks &operator=(IOCallbacks &&) = delete;

    std::vector<cb_func_t> _cbs;
  };

  class IOCallbackManager
  {
  public:
    static constexpr int IOV_CACHE_BUF = 4096;
    IOCallbackManager() {}
    virtual ~IOCallbackManager() {}

    int Register(int fd)
    {
      if (fd <= 0)
      {
        assert(false);
        return -1;
      }

      while (_cbs.size() < static_cast<size_t>(fd + 1))
      {
        _cbs.resize(_cbs.size() + 32, nullptr);
        _iov_cache.resize(_iov_cache.size() + 32, nullptr);
        _iov_buf.resize(_iov_buf.size() + 32, nullptr);
      }

      assert(static_cast<size_t>(fd) < _cbs.capacity());
      assert(_cbs[fd].get() == nullptr);

      _cbs[fd] = std::make_shared<IOCallbacks>();
      _iov_cache[fd] = std::make_shared<struct iovec>();
      _iov_buf[fd] = std::make_shared<char>();
      _iov_buf[fd].reset(new char[IOV_CACHE_BUF]);

      _iov_cache[fd]->iov_base = _iov_buf[fd].get();
      _iov_cache[fd]->iov_len = IOV_CACHE_BUF;

      return 0;
    }

    // simple write cache for testing
    std::shared_ptr<struct iovec> &GetWriteCache(int fd)
    {
      assert(static_cast<size_t>(fd) < _iov_cache.size());
      return _iov_cache[fd];
    }

    int Unregister(int fd)
    {
      if (fd <= 0)
      {
        assert(false);
        return -1;
      }

      if (static_cast<size_t>(fd) < _cbs.size())
      {
        _cbs[fd].reset();
        _iov_cache[fd].reset();
        _iov_buf[fd].reset();
      }

      return 0;
    }

    void SetCallback(int fd, io_uring_op cb, IOCallbacks::cb_func_t &f)
    {
      assert(static_cast<size_t>(fd) < _cbs.size());
      assert(_cbs[fd].get() != nullptr);
      _cbs[fd]->SetCallback(cb, f);
    }

    void Fire(int fd, io_uring_op cb, int res, int flags)
    {
      assert(static_cast<size_t>(fd) < _cbs.size());
      assert(_cbs[fd].get() != nullptr);
      _cbs[fd]->Fire(cb, fd, res, flags);
    }

  private:
    IOCallbackManager(const IOCallbackManager &) = delete;
    IOCallbackManager &operator=(const IOCallbackManager &) = delete;
    IOCallbackManager(IOCallbackManager &&) = delete;
    IOCallbackManager &operator=(IOCallbackManager &&) = delete;

    std::vector<std::shared_ptr<IOCallbacks>> _cbs;

    std::vector<std::shared_ptr<struct iovec>> _iov_cache;
    std::vector<std::shared_ptr<char>> _iov_buf;
  };

  class IOBufManager
  {
  public:
    IOBufManager(uint16_t group_id,
                 int num_bufs,
                 uint32_t buf_size = 4096) : _used_count(0), _buf_size(buf_size), _group_id(group_id), _num_bufs(num_bufs), _buffers(nullptr)
    {
    }

    virtual ~IOBufManager()
    {
    }

    int Init()
    {
      size_t pageSize = coypu::mem::MemManager::GetPageSize();
      if ((_buf_size * _num_bufs) % pageSize)
      {
        printf("Buffer size * num buffers must be a multiple of page size %zu\n", pageSize);
        return -1;
      }

      int r = posix_memalign(&_buffers, 4096, _buf_size * _num_bufs);
      if (r != 0)
      {
        perror("posix_memalign");
        return -2;
      }

      return 0;
    }

    inline uint16_t GetGroupID() const
    {
      return _group_id;
    }

    inline int GetNumBufs() const
    {
      return _num_bufs;
    }

    inline uint32_t GetBufSize() const
    {
      return _buf_size;
    }

    inline char *GetBuffers() const
    {
      return reinterpret_cast<char *>(_buffers);
    }

    int GetUsedCount() const
    {
      return _used_count;
    }

    void IncUsedCount()
    {
      _used_count++;
    }

    void Reset()
    {
      _used_count = 0;
    }

    bool IsFull() const
    {
      return _used_count == _num_bufs;
    }

    int SubmitBuffers(coypu_io_uring &ring, struct IOCallback &cb_buffers)
    {
      return IOURingHelper::SubmitProvideBuffers(ring,
                                                 GetBuffers(),
                                                 GetNumBufs(),
                                                 GetBufSize(),
                                                 GetGroupID(),
                                                 *reinterpret_cast<uint64_t *>(&cb_buffers));
    }

  private:
    IOBufManager(const IOBufManager &) = delete;
    IOBufManager &operator=(const IOBufManager &) = delete;
    IOBufManager(IOBufManager &&) = delete;
    IOBufManager &operator=(IOBufManager &&) = delete;

    int _used_count;
    uint32_t _buf_size;
    uint16_t _group_id;
    int _num_bufs;
    void *_buffers;
  };

} //  coypu::event
