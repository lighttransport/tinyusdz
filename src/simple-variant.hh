// https://gist.github.com/calebh/fd00632d9c616d4b0c14e7c2865f3085

/*
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.
In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
For more information, please refer to <http://unlicense.org/>
*/

#include <iostream>
#include <string>

namespace tinyusdz {

// Equivalent to std::aligned_storage
template<unsigned int Len, unsigned int Align>
struct aligned_storage {
	struct type {
		alignas(Align) unsigned char data[Len];
	};
};

template <unsigned int arg1, unsigned int ... others>
struct static_max;

template <unsigned int arg>
struct static_max<arg>
{
	static const unsigned int value = arg;
};

template <unsigned int arg1, unsigned int arg2, unsigned int ... others>
struct static_max<arg1, arg2, others...>
{
	static const unsigned int value = arg1 >= arg2 ? static_max<arg1, others...>::value :
		static_max<arg2, others...>::value;
};

template<class T> struct remove_reference { typedef T type; };
template<class T> struct remove_reference<T&> { typedef T type; };
template<class T> struct remove_reference<T&&> { typedef T type; };

template<uint8_t n, typename... Ts>
struct variant_helper_rec;

template<uint8_t n, typename F, typename... Ts>
struct variant_helper_rec<n, F, Ts...> {
	inline static void destroy(uint8_t id, void* data)
	{
		if (n == id) {
			reinterpret_cast<F*>(data)->~F();
		} else {
			variant_helper_rec<n + 1, Ts...>::destroy(id, data);
		}
	}

	inline static void move(uint8_t id, void* from, void* to)
	{
		if (n == id) {
			// This static_cast and use of remove_reference is equivalent to the use of std::move
			new (to) F(static_cast<typename remove_reference<F>::type&&>(*reinterpret_cast<F*>(from)));
		} else {
			variant_helper_rec<n + 1, Ts...>::move(id, from, to);
		}
	}

	inline static void copy(uint8_t id, const void* from, void* to)
	{
		if (n == id) {
			new (to) F(*reinterpret_cast<const F*>(from));
		} else {
			variant_helper_rec<n + 1, Ts...>::copy(id, from, to);
		}
	}
};

template<uint8_t n> struct variant_helper_rec<n> {
	inline static void destroy(uint8_t id, void* data) { }
	inline static void move(uint8_t old_t, void* from, void* to) { }
	inline static void copy(uint8_t old_t, const void* from, void* to) { }
};

template<typename... Ts>
struct variant_helper {
	inline static void destroy(uint8_t id, void* data) {
		variant_helper_rec<0, Ts...>::destroy(id, data);
	}

	inline static void move(uint8_t id, void* from, void* to) {
		variant_helper_rec<0, Ts...>::move(id, from, to);
	}

	inline static void copy(uint8_t id, const void* old_v, void* new_v) {
		variant_helper_rec<0, Ts...>::copy(id, old_v, new_v);
	}
};

template<> struct variant_helper<> {
	inline static void destroy(uint8_t id, void* data) { }
	inline static void move(uint8_t old_t, void* old_v, void* new_v) { }
	inline static void copy(uint8_t old_t, const void* old_v, void* new_v) { }
};

template<typename F>
struct variant_helper_static;

template<typename F>
struct variant_helper_static {
	inline static void move(void* from, void* to) {
		new (to) F(static_cast<typename remove_reference<F>::type&&>(*reinterpret_cast<F*>(from)));
	}

	inline static void copy(const void* from, void* to) {
		new (to) F(*reinterpret_cast<const F*>(from));
	}
};

// Given a uint8_t i, selects the ith type from the list of item types
template<uint8_t i, typename... Items>
struct variant_alternative;

template<typename HeadItem, typename... TailItems>
struct variant_alternative<0, HeadItem, TailItems...>
{
	using type = HeadItem;
};

template<uint8_t i, typename HeadItem, typename... TailItems>
struct variant_alternative<i, HeadItem, TailItems...>
{
	using type = typename variant_alternative<i - 1, TailItems...>::type;
};

template<typename... Ts>
struct variant {
private:
	static const unsigned int data_size = static_max<sizeof(Ts)...>::value;
	static const unsigned int data_align = static_max<alignof(Ts)...>::value;

	using data_t = typename aligned_storage<data_size, data_align>::type;

	using helper_t = variant_helper<Ts...>;

	template<uint8_t i>
	using alternative = typename variant_alternative<i, Ts...>::type;

	uint8_t variant_id;
	data_t data;

	variant(uint8_t id) : variant_id(id) {}

public:
	template<uint8_t i>
	static variant create(alternative<i>& value)
	{
		variant ret(i);
		variant_helper_static<alternative<i>>::copy(&value, &ret.data);
		return ret;
	}

	template<uint8_t i>
	static variant create(alternative<i>&& value) {
		variant ret(i);
		variant_helper_static<alternative<i>>::move(&value, &ret.data);
		return ret;
	}

	variant(const variant<Ts...>& from) : variant_id(from.variant_id)
	{
		helper_t::copy(from.variant_id, &from.data, &data);
	}

	variant(variant<Ts...>&& from) : variant_id(from.variant_id)
	{
		helper_t::move(from.variant_id, &from.data, &data);
	}

	variant<Ts...>& operator= (variant<Ts...>& rhs)
	{
		helper_t::destroy(variant_id, &data);
		variant_id = rhs.variant_id;
		helper_t::copy(rhs.variant_id, &rhs.data, &data);
		return *this;
	}

	variant<Ts...>& operator= (variant<Ts...>&& rhs)
	{
		helper_t::destroy(variant_id, &data);
		variant_id = rhs.variant_id;
		helper_t::move(rhs.variant_id, &rhs.data, &data);
		return *this;
	}

	uint8_t id() {
		return variant_id;
	}

	template<uint8_t i>
	void set(alternative<i>& value)
	{
		helper_t::destroy(variant_id, &data);
		variant_id = i;
		variant_helper_static<alternative<i>>::copy(&value, &data);
	}

	template<uint8_t i>
	void set(alternative<i>&& value)
	{
		helper_t::destroy(variant_id, &data);
		variant_id = i;
		variant_helper_static<alternative<i>>::move(&value, &data);
	}

	//template<uint8_t i>
	//alternative<i>& get()
	//{
	//	if (variant_id == i) {
	//		return *reinterpret_cast<alternative<i>*>(&data);
	//	} else {
  //    // Replace std::bad_cast with something else if the standard library is not available
	//		throw std::bad_cast();
	//	}
	//}

	template<uint8_t i>
	alternative<i>* get_if()
	{
		if (variant_id == i) {
		  return reinterpret_cast<alternative<i>*>(&data);
		} else {
      return nullptr;
		}
	}


	~variant() {
		helper_t::destroy(variant_id, &data);
	}
};

} // namespace tinyusdz
