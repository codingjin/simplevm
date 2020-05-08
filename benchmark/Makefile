all: multi_test
#test: ../my_vm.h
	#gcc test2.c -L../ -lmy_vm -o test2 -m64 -pthread
	#gcc test1.c -L../ -lmy_vm -m32 -o test1
test: ../my_vm.h
	gcc -std=gnu99 -o test test.c -L../ -lmy_vm -m64

multi_test: ../my_vm.h
	gcc -std=gnu99 -o multi_test multi_test.c -L../ -lmy_vm -m64 -pthread

clean:
	rm -rf test multi_test
