
gcc -c keyhook.c
gcc -shared -o keyhook.dll keyhook.o

windres -o resources.o resources.rc
gcc -o SuperF4 superf4.c resources.o -mwindows

strip SuperF4.exe
strip keyhook.dll

upx -9 -q SuperF4.exe
upx -9 -q keyhook.dll
