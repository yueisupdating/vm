exe: main.c module/vmx.h
	gcc -o exe main.c -I./module

.PHONY: clean

clean:
	rm -rf exe
