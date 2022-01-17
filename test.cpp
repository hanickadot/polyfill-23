#include <hana23/move_only_function.hpp>
#include <memory>
#include <cstdio>

struct foo {
	int a() { return 1; }
	int b() { return 2; }
};

int main() {
	hana23::move_only_function<int(void)> f = [i = 0ull]() mutable {
		printf("this = %p, value = %llu\n", &i, i);
		return ++i;
	};

	printf("%d\n", f());
	printf("%d\n", f());
	printf("%d\n", f());

	using ptr = int (foo::*)();

	ptr p = &foo::b;

	hana23::move_only_function<int(foo)> f2 = p;

	printf("%d\n", f2(foo()));
}