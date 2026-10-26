#include "ecsModule/componentArray.h"
#include "renderModule/backends/interface/reflectionStructs.h"
#include "utilsModule/chunkList.h"
#include "utilsModule/containers/flatMap.h"
#include "utilsModule/containers/internedNameMap.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <thread>

using namespace IKIGAI;
using namespace IKIGAI::UTILS;
using namespace IKIGAI::MEMORY;
#define CHECK(...)                                                                                                                                   \
	do {                                                                                                                                             \
		if (!(__VA_ARGS__))                                                                                                                          \
			throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                                        \
	} while (false)
template <class Exception, class F> void Throws(F&& fn) {
	try {
		fn();
	} catch (const Exception&) {
		return;
	}
	throw std::runtime_error("Expected exception");
}

// Validates domain ownership and failure cleanup, not merely allocation counts.
struct Domain : IAllocator {
	HeapAllocator heap;
	std::unordered_map<void*, std::size_t> live;
	int failAfter = -1;
	std::size_t allocations = 0, peak = 0, bytes = 0;
	~Domain() {
		if (!live.empty()) {
			std::terminate();
		}
	}
	void* allocate(std::size_t n) override {
		return allocate(n, alignof(std::max_align_t));
	}
	void* allocate(std::size_t n, std::size_t alignment) override {
		if (failAfter == 0) {
			throw std::bad_alloc();
		}
		if (failAfter > 0) {
			--failAfter;
		}
		void* ptr = heap.allocate(n, alignment);
		try {
			live.emplace(ptr, n);
		} catch (...) {
			heap.deallocate(ptr);
			throw;
		}
		++allocations;
		bytes += n;
		peak = std::max(peak, bytes);
		return ptr;
	}
	void deallocate(void* ptr) override {
		if (!ptr) {
			return;
		}
		auto it = live.find(ptr);
		if (it == live.end()) {
			std::terminate();
		}
		bytes -= it->second;
		live.erase(it);
		heap.deallocate(ptr);
	}
};
template <class T> struct LocalAllocator {
	using value_type = T;
	using is_always_equal = std::false_type;
	std::shared_ptr<Domain> domain;
	explicit LocalAllocator(std::shared_ptr<Domain> d)
		: domain(std::move(d)) { }
	template <class U>
	LocalAllocator(const LocalAllocator<U>& other)
		: domain(other.domain) { }
	T* allocate(std::size_t n) {
		return static_cast<T*>(domain->allocate(n * sizeof(T), alignof(T)));
	}
	void deallocate(T* ptr, std::size_t) {
		domain->deallocate(ptr);
	}
	template <class U> bool operator==(const LocalAllocator<U>& other) const {
		return domain == other.domain;
	}
};
struct alignas(128) Immovable {
	static inline int live = 0;
	int value;
	explicit Immovable(int v)
		: value(v) {
		if (v < 0) {
			throw std::runtime_error("constructor");
		}
		++live;
	}
	~Immovable() {
		--live;
	}
	Immovable(const Immovable&) = delete;
	Immovable(Immovable&&) = delete;
};
struct Fragile {
	static inline bool fail = false;
	int value = 0;
	explicit Fragile(int v = 0)
		: value(v) { }
	Fragile(const Fragile& other)
		: value(other.value) {
		if (fail) {
			throw std::runtime_error("copy");
		}
	}
	Fragile(Fragile&& other) noexcept(false)
		: value(other.value) {
		if (fail) {
			throw std::runtime_error("move");
		}
	}
	Fragile& operator=(const Fragile& other) {
		if (fail) {
			throw std::runtime_error("copy assign");
		}
		value = other.value;
		return *this;
	}
	Fragile& operator=(Fragile&& other) noexcept(false) {
		if (fail) {
			throw std::runtime_error("move assign");
		}
		value = other.value;
		return *this;
	}
};

void TestIds() {
	struct A { };
	struct B { };
	static_assert(!std::is_convertible_v<Id<A>, Id<B>>);
	static_assert(!std::is_convertible_v<Id<A>, unsigned>);
	static_assert(!Id<A> {}.valid() && Id<A>(0).valid());
	CHECK(std::hash<Id<A>> {}(Id<A>(42)) == std::hash<std::uint32_t> {}(42));
	Throws<std::out_of_range>([] { (void)static_cast<int>(Id<A> {}); });
	Throws<std::out_of_range>([] { Id<A> negative(-1); });
	Throws<std::out_of_range>([] { Id<A> wide(std::uint64_t { 1 } << 40); });
	IdSequence<A, std::uint8_t, 1, 3> small;
	CHECK(small.next().value() == 1);
	small.observeLoaded(Id<A, std::uint8_t>(2));
	CHECK(small.next().value() == 3);
	Throws<std::overflow_error>([&] { small.next(); });
	Throws<std::overflow_error>([&] { small.next(); });
	Throws<std::out_of_range>([&] { small.observeLoaded({}); });
	CHECK(ECS::Entity().getUniqueId().value() > 0);
	ECS::Entity loaded(ECS::Entity::ID(100000));
	auto copy = loaded;
	CHECK(copy == loaded);
	CHECK(ECS::Entity().getUniqueId().value() > 100000);
	CHECK(IdGenerator<B>::generateId().value() == 0);
	CHECK(ECS::Entity(ECS::Entity::ID(0)).getUniqueId().value() == 0);
	Throws<std::out_of_range>([] { ECS::Entity bad(ECS::Entity::ID(0xffffffffu)); });
	IdSequence<A> concurrent;
	std::array<std::vector<std::uint32_t>, 4> ids;
	std::vector<std::thread> workers;
	for (int t = 0; t < 4; ++t) {
		workers.emplace_back([&, t] {
			for (int i = 0; i < 2000; ++i) {
				if (i % 50 == 0) {
					concurrent.observeLoaded(Id<A>(static_cast<std::uint32_t>(i * 20)));
				}
				ids[t].push_back(concurrent.next().value());
			}
		});
	}
	for (auto& thread : workers) {
		thread.join();
	}
	std::set<std::uint32_t> unique;
	for (const auto& values : ids) {
		unique.insert(values.begin(), values.end());
	}
	CHECK(unique.size() == 8000);
}

void TestChunks() {
	static_assert(std::random_access_iterator<ChunkedVector<int>::iterator>);
	static_assert(std::random_access_iterator<ChunkedVector<int>::const_iterator>);
	static_assert(std::is_same_v<decltype(*std::declval<const ChunkedVector<int>&>().begin()), const int&>);
	auto domain = std::make_shared<Domain>();
	{
		ChunkedVector<Immovable, 4> items { StlAllocator<Immovable>(domain) };
		auto* first = &items.emplace_back(7);
		auto it = items.begin();
		CHECK(reinterpret_cast<std::uintptr_t>(first) % 128 == 0);
		for (int i = 0; i < 32; ++i) {
			items.emplace_back(i);
		}
		CHECK(&*it == first && first->value == 7);
		items.reserve(1000);
		CHECK(&items[0] == first);
		auto size = items.size();
		Throws<std::runtime_error>([&] { items.emplace_back(-1); });
		CHECK(items.size() == size);
		CHECK(items.chunk_count() == 9 && items.chunk(8).size() == 1);
		Throws<std::out_of_range>([&] { items.at(items.size()); });
		Throws<std::out_of_range>([&] { items.at(static_cast<std::size_t>(-1)); });
		auto moved = std::move(items);
		CHECK(&moved[0] == first && items.empty());
		auto capacity = moved.capacity();
		moved.clear();
		CHECK(Immovable::live == 0 && moved.capacity() == capacity);
		moved.emplace_back(9);
		moved.clear();
		moved.release();
		CHECK(moved.capacity() == 0);
		items.emplace_back(11); // moved-from storage remains usable
	}
	CHECK(Immovable::live == 0 && domain->live.empty());
	for (int size : { 0, 1, 3, 4, 5, 8, 100, 200, 201 }) {
		ChunkedVector<int, 4> values;
		for (int i = 0; i < size; ++i) {
			values.emplace_back(i);
		}
		CHECK(std::accumulate(values.begin(), values.end(), 0) == size * (size - 1) / 2);
		auto copy = values;
		copy = copy;
		CHECK(copy.size() == values.size());
		CHECK(std::equal(copy.begin(), copy.end(), values.begin()));
	}
	{
		ChunkedVector<int, 4> values { StlAllocator<int>(domain) };
		values.emplace_back(31);
		auto* ptr = &values[0];
		domain->failAfter = 0;
		Throws<std::bad_alloc>([&] { values.reserve(100); });
		CHECK(values.size() == 1 && &values[0] == ptr && values[0] == 31);
		domain->failAfter = -1;
		ChunkList<int, std::vector, 4> legacy;
		for (int i = 0; i < 8; ++i) {
			legacy.push_back(i);
		}
		CHECK(std::distance(legacy.begin(), legacy.end()) == 8);
		legacy.moveTo(2, 43);
		CHECK(legacy.moveFrom(2) == 43);
	}
	CHECK(domain->live.empty());
	auto otherDomain = std::make_shared<Domain>();
	{
		ChunkedVector<std::unique_ptr<int>, 2> values;
		values.emplace_back(std::make_unique<int>(31));
		auto moved = std::move(values);
		CHECK(*moved[0] == 31);
		ChunkedVector<int, 4> a { StlAllocator<int>(domain) }, b { StlAllocator<int>(otherDomain) };
		a.emplace_back(12);
		b.emplace_back(13);
		a.swap(b);
		CHECK(a[0] == 13 && b[0] == 12);
		b = std::move(a);
		CHECK(b[0] == 13 && a.empty());
	}
	{
		using V = ChunkedVector<int, 4, LocalAllocator<int>>;
		V a { LocalAllocator<int>(domain) }, b { LocalAllocator<int>(otherDomain) };
		a.emplace_back(7);
		b.emplace_back(8);
		b = a;
		CHECK(b.get_allocator().domain == otherDomain && b[0] == 7);
		b = std::move(a);
		CHECK(b[0] == 7 && a.empty());
		Throws<std::invalid_argument>([&] { a.swap(b); });
	}
	{
		ChunkedVector<Fragile, 4> values;
		values.emplace_back(1);
		ChunkedVector<Fragile, 4> target;
		target.emplace_back(2);
		Fragile::fail = true;
		Throws<std::runtime_error>([&] { target = values; });
		Fragile::fail = false;
		CHECK(target.size() == 1 && target[0].value == 2);
	}
}

void TestFlatMap() {
	FlatMap<int, int> values;
	static_assert(std::is_const_v<std::remove_reference_t<decltype(values.begin()->first)>>);
	static_assert(std::is_same_v<decltype(std::as_const(values).begin()->second), const int&>);
	std::map<int, int> reference;
	std::mt19937 random(82);
	for (int i = 0; i < 5000; ++i) {
		int key = random() % 120, value = random() % 1000;
		switch (random() % 3) {
		case 0:
			values.insert_or_assign(key, value);
			reference.insert_or_assign(key, value);
			break;
		case 1:
			CHECK(values.erase(key) == reference.erase(key));
			break;
		default:
			CHECK(values.contains(key) == reference.contains(key));
			break;
		}
		CHECK(values.size() == reference.size());
		auto expected = reference.begin();
		for (auto [k, v] : values) {
			CHECK(k == expected->first && v == expected->second);
			++expected;
		}
	}
	std::vector<std::pair<int, int>> bulk { { 7, 2 }, { 1, 3 } };
	values.assign(bulk.begin(), bulk.end());
	CHECK(values.at(7) == 2 && values.begin()->first == 1);
	bulk.emplace_back(1, 4);
	Throws<std::invalid_argument>([&] { values.assign(bulk.begin(), bulk.end()); });
	CHECK(values.size() == 2);
	FlatMap<std::string, std::unique_ptr<int>> pointers;
	pointers.try_emplace("abc", std::make_unique<int>(7));
	CHECK(*pointers.at(std::string_view("abc")) == 7);
	auto owned = std::make_unique<int>(11);
	CHECK(!pointers.try_emplace("abc", std::move(owned)).second && owned);
	FlatMap<int, Fragile> fragile;
	fragile.try_emplace(1, 2);
	fragile.try_emplace(3, 4);
	Fragile::fail = true;
	Throws<std::runtime_error>([&] { fragile.try_emplace(2, 3); });
	Throws<std::runtime_error>([&] { fragile.erase(1); });
	Fragile::fail = false;
	CHECK(fragile.size() == 2 && fragile.at(1).value == 2 && fragile.at(3).value == 4);
	FlatMap<int, int, std::greater<>> descending;
	descending[2] = 2;
	descending[5] = 5;
	CHECK(descending.begin()->first == 5);
	FlatMap<int, int> normalized;
	normalized[1] = 7;
	CHECK(!normalized.try_emplace(1.8, 9).second && normalized.size() == 1);
	std::vector<std::pair<std::string, std::unique_ptr<int>>> moving;
	moving.emplace_back("x", std::make_unique<int>(3));
	pointers.assign(std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
	CHECK(*pointers.at("x") == 3 && !moving[0].second);
	auto a = std::make_shared<Domain>(), b = std::make_shared<Domain>();
	{
		using Map = FlatMap<int, int, std::less<>, LocalAllocator<std::pair<int, int>>>;
		Map source { typename Map::allocator_type(a) }, target { typename Map::allocator_type(b) };
		source[5] = 8;
		target[3] = 4;
		target = source;
		CHECK(target.at(5) == 8 && target.get_allocator().domain == b);
		target = std::move(source);
		CHECK(target.at(5) == 8 && source.empty());
		Throws<std::invalid_argument>([&] { source.swap(target); });
	}
	CHECK(a->live.empty() && b->live.empty());
}

void TestMemory() {
	struct Aggregate {
		ChunkedVector<int> values;
		FlatMap<int, int> map;
		StringPool strings;
		SynchronizedStringPool synchronized;
		MonotonicAllocator arena;
		ScratchMemory scratch;
	} aggregate {};
	CHECK(aggregate.values.empty() && aggregate.map.empty() && aggregate.strings.size() == 0);
	auto a = std::make_shared<Domain>(), b = std::make_shared<Domain>();
	{
		std::vector<int, StlAllocator<int>> x { StlAllocator<int>(a) }, y { StlAllocator<int>(b) };
		x.push_back(1);
		y.push_back(2);
		y = x;
		CHECK(y.get_allocator().domain() == b);
		y = std::move(x);
		CHECK(y.get_allocator().domain() == a && y[0] == 1);
		std::vector<int, StlAllocator<int>> z { StlAllocator<int>(b) };
		z.push_back(3);
		y.swap(z);
		CHECK(z[0] == 1 && y[0] == 3);
		StlAllocator<double> rebound { y.get_allocator() };
		CHECK(rebound.domain() == b);
		Throws<std::invalid_argument>([] { StlAllocator<int> invalid { std::shared_ptr<IAllocator> {} }; });
	}
	CHECK(a->live.empty() && b->live.empty());
	const auto before = PROFILER::MemoryProfiler::instance().captureSnapshot();
	{
		MonotonicAllocator arena(256, a, 2048);
		auto* p = static_cast<int*>(arena.allocate(sizeof(int), 128));
		*p = 77;
		CHECK(reinterpret_cast<std::uintptr_t>(p) % 128 == 0);
		for (int i = 0; i < 10; ++i) {
			arena.allocate(30, 16);
		}
		CHECK(*p == 77);
		auto reserved = arena.stats().reserved;
		Throws<std::bad_alloc>([&] { arena.allocate(4096); });
		arena.reset();
		CHECK(arena.stats().used == 0 && arena.stats().allocations == 0);
		arena.allocate(4);
		CHECK(arena.stats().reserved == reserved);
		arena.release();
		CHECK(arena.stats().reserved == 0);
	}
	CHECK(a->live.empty());
	ScratchMemory scratch;
	{
		std::vector<int, StlAllocator<int>> temp { scratch.allocator<int>() };
		temp.push_back(7);
		Throws<std::logic_error>([&] { scratch.reset(); });
	}
	scratch.reset();
	CHECK(scratch.stats().used == 0);
	// A tracked upstream owns the backing records: no second logical RAM count.
	{
		auto upstream = BufferAllocator<Category::General>();
		MonotonicAllocator arena(256, upstream);
		arena.allocate(4, 1);
		arena.allocate(3, 1);
		auto active = PROFILER::MemoryProfiler::instance().captureSnapshot();
		for (const auto& allocation : active.allocations) {
			CHECK(allocation.allocator != &arena);
		}
		CHECK(upstream->tracksAllocations() == (IKIGAI_ENABLE_MEMORY_TRACKING != 0));
		arena.reset();
	}
	const auto after = PROFILER::MemoryProfiler::instance().captureSnapshot();
	CHECK(after.droppedRecords == before.droppedRecords);
	for (std::size_t i = 0; i < after.categories.size(); ++i) {
		CHECK(after.categories[i].bytes == before.categories[i].bytes);
		CHECK(after.categories[i].allocations == before.categories[i].allocations);
	}
	for (const auto& allocation : after.allocations) {
		CHECK(allocation.allocator != a.get());
	}
}

struct CollisionHash {
	std::size_t operator()(std::string_view) const noexcept {
		return 1;
	}
};
void TestStrings() {
	auto domain = std::make_shared<Domain>();
	{
		BasicStringPool<CollisionHash> pool(domain, 64);
		auto empty = pool.intern("");
		CHECK(empty.valid() && pool.view(empty).empty());
		std::string original = "position";
		auto id = pool.intern(original);
		original.assign("changed");
		CHECK(pool.view(id) == "position" && pool.intern("position") == id);
		auto view = pool.view(id);
		const auto* address = view.data();
		CHECK(pool.intern("Position") != id);
		auto nul = pool.intern(std::string_view("a\0b", 3));
		CHECK(pool.view(nul).size() == 3);
		auto utf = pool.intern("\xD0\x98\xD0\xBC\xD1\x8F");
		CHECK(pool.view(utf).size() == 6);
		for (int i = 0; i < 1000; ++i) {
			pool.intern("uniform_" + std::to_string(i));
		}
		CHECK(pool.view(id).data() == address && view == "position");
		CHECK(!pool.find("absent").valid());
		StringPool foreign;
		Throws<std::out_of_range>([&] { foreign.view(id); });
		CHECK(foreign.intern("position") != id);
		domain->failAfter = 0;
		Throws<std::bad_alloc>([&] { pool.intern(std::string(5000, 'x')); });
		domain->failAfter = -1;
		CHECK(pool.find("position") == id && !pool.find(std::string(5000, 'x')).valid());
	}
	CHECK(domain->live.empty());
	for (int failure = 0; failure < 8; ++failure) {
		auto failing = std::make_shared<Domain>();
		StringPool pool(failing, 32);
		auto original = pool.intern("seed");
		const auto view = pool.view(original);
		failing->failAfter = failure;
		try {
			pool.intern(std::string(300, 'z'));
		} catch (const std::bad_alloc&) { }
		failing->failAfter = -1;
		auto id = pool.intern(std::string(300, 'z'));
		CHECK(pool.size() == 2 && pool.view(id).size() == 300 && view == "seed");
	}
	SynchronizedStringPool pool;
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t) {
		threads.emplace_back([&] {
			for (int i = 0; i < 200; ++i) {
				auto id = pool.intern("name" + std::to_string(i));
				CHECK(pool.view(id) == "name" + std::to_string(i));
				CHECK(pool.find(pool.view(id)) == id);
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}
	CHECK(pool.size() == 200);
	RENDER::ShaderReflection reflection;
	reflection.mNameToUniforms["engine_UBO"] = 3;
	const auto id = reflection.mNameToUniforms.identifier("engine_UBO");
	CHECK(reflection.mNameToUniforms.at(id) == 3 && reflection.mNameToUniforms.at("engine_UBO") == 3);
	auto copy = reflection;
	reflection = RENDER::ShaderReflection {};
	CHECK(copy.mNameToUniforms.name(id) == "engine_UBO" && copy.mNameToUniforms.at(id) == 3);
	CHECK(!reflection.mNameToUniforms.contains(id));
	auto moved = std::move(copy);
	CHECK(moved.mNameToUniforms.at(id) == 3);
	copy.mNameToUniforms["new"] = 8;
	CHECK(copy.mNameToUniforms.at("new") == 8);
}

// Exercises the real ECS storage and weak control-block update protocol without
// requiring a renderer/world. ComponentArray itself accepts this movable payload.
struct TestComponent : ControlBlockHandler {
	int value;
	explicit TestComponent(int v)
		: value(v) { }
	template <class T> WeakPtr<T> getWeak() {
		return WeakPtr<T>(*this);
	}
};
void TestEcs() {
	ECS::ComponentArray<TestComponent, std::vector, 4> values;
	std::vector<ECS::Entity> entities;
	for (int i = 0; i < 8; ++i) {
		entities.emplace_back();
		values.insert(entities.back(), TestComponent(i));
	}
	auto first = values.getDataPtr(entities[0]), last = values.getDataPtr(entities[7]);
	CHECK(std::distance(values.begin(), values.end()) == 8);
	auto removed = values.removeData(entities[0]);
	CHECK(!first && last && last->value == 7);
	CHECK(last.get() == &values.getData(entities[7]));
	auto lastRemoved = values.removeData(entities[6]);
	CHECK(lastRemoved.value == 6 && values.getSize() == 6);
}
void TestSmallVector();
int main() {
	try {
		TestIds();
		TestChunks();
		TestSmallVector();
		TestFlatMap();
		TestMemory();
		TestStrings();
		TestEcs();
		std::cout << "Utility regressions passed (tracking=" << IKIGAI_ENABLE_MEMORY_TRACKING << ")\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
