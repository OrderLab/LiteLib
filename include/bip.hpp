#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
namespace bip = boost::interprocess;
using SharedMemory = bip::managed_shared_memory;
using SegmentManager = SharedMemory::segment_manager;
template <typename T>
using ShmAllocator = bip::allocator<T, SegmentManager>;
using ShmVoidAllocator = bip::allocator<void, SegmentManager>;
template <typename T>
using ShmDeleter = bip::deleter<T, SegmentManager>;

#include <boost/atomic/ipc_atomic.hpp>
#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/pair.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>

template <typename T>
using ShmAtomic = boost::ipc_atomic<T>;

using ShmString =
    bip::basic_string<char, std::char_traits<char>, ShmAllocator<char>>;
template <class T>
using ShmVector = bip::vector<T, ShmAllocator<T>>;
template <class Key, class Value>
using ShmMap = bip::map<Key, Value, std::less<Key>,
                        ShmAllocator<bip::pair<const Key, Value>>>;
template <class T>
using ShmSet = bip::set<T, std::less<T>, ShmAllocator<T>>;
template <typename T>
using ShmDeque = bip::deque<T, ShmAllocator<T>>;
template <class T>
using ShmSharedPtr =
    typename bip::managed_shared_ptr<T, bip::managed_shared_memory>::
        type;  // TODO(test): if full crash, will the ref count be correct?
#define ShmMakeShared(constructed_object, shm) \
  bip::make_managed_shared_ptr((constructed_object), (shm))
template <class T>
using ShmUniquePtr =
    typename bip::managed_unique_ptr<T, bip::managed_shared_memory>::type;
#define ShmMakeUnique(constructed_object, shm) \
  bip::make_managed_unique_ptr((constructed_object), (shm))
template <class T, class Deleter>
using ShmUniquePtrWithDeleter = typename bip::unique_ptr<T, Deleter>;