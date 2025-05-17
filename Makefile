EXEC=miniEXT2
DIR=mount_point

fuse_flags= -D_FILE_OFFSET_BITS=64 -lfuse -pthread

.PHONY: mount umount debug clean

$(EXEC): miniEXT2.o
	gcc -g -o $@  $^ ${fuse_flags}
	mkdir -p $(DIR)
	
miniEXT2.o: miniEXT2.c
	gcc -g -c -o $@  $< ${fuse_flags}

mount: miniEXT2
	./miniEXT2 $(DIR)

debug: miniEXT2
	./miniEXT2 -d $(DIR)

umount:
	fusermount -u $(DIR)

clean:
	rm $(EXEC) *.o
	rmdir $(DIR)
