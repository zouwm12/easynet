#编译目录下的所有.c文件

CC = gcc
#wildcard 获取目录下所有.c文件  patsubst：将.c替换为.o
OBJ := $(patsubst %.c,%.o,$(wildcard *.c))
INCLUDE := -I ./
LIB :=
CFLAGS:= -g -O0 -Wall
TARGET := easynet

# $@            --代表目标文件(target)
# $^            --代表所有的依赖文件(components)
# $<            --代表第一个依赖文件(components中最左边的那个)。
all: $(TARGET) 
$(TARGET): $(OBJ)
	$(CC) $(INCLUDE) $(CFLAGS) -o $@ $^ $(LIB)

#.o文件生成规则
%.o:%.c
	$(CC) $(INCLUDE) $(CFLAGS) -o $@ -c $< 

.PHONY:clean
clean:
	-rm -rf $(TARGET) *.o