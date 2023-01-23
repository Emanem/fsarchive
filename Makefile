#Makefile generated by amake
#On Mon Jan 23 18:00:09 2023
#To print amake help use 'amake --help'.
CC=gcc
CPPC=g++
LINK=g++
SRCDIR=./src
OBJDIR=obj
FLAGS=-g -Wall 
LIBS=-lzip 
OBJS=$(OBJDIR)/zip_fs.o $(OBJDIR)/bspatch.o $(OBJDIR)/main.o $(OBJDIR)/log.o $(OBJDIR)/fsarchive.o $(OBJDIR)/bsdiff.o $(OBJDIR)/settings.o 
EXEC=fsarchive
DATE=$(shell date +"%Y-%m-%d")

$(EXEC) : $(OBJS)
	$(LINK) $(OBJS) -o $(EXEC) $(FLAGS) $(LIBS)

$(OBJDIR)/zip_fs.o: src/zip_fs.cpp src/zip_fs.h src/log.h src/utils.h $(OBJDIR)/__setup_obj_dir
	$(CPPC) $(FLAGS) ./src/zip_fs.cpp -c -o $@

$(OBJDIR)/bspatch.o: src/bspatch.c src/bspatch.h $(OBJDIR)/__setup_obj_dir
	$(CC) $(FLAGS) ./src/bspatch.c -c -o $@

$(OBJDIR)/main.o: src/main.cpp src/settings.h src/fsarchive.h src/log.h src/utils.h $(OBJDIR)/__setup_obj_dir
	$(CPPC) $(FLAGS) ./src/main.cpp -c -o $@

$(OBJDIR)/log.o: src/log.cpp src/log.h src/utils.h $(OBJDIR)/__setup_obj_dir
	$(CPPC) $(FLAGS) ./src/log.cpp -c -o $@

$(OBJDIR)/fsarchive.o: src/fsarchive.cpp src/fsarchive.h src/settings.h src/utils.h \
 src/log.h src/zip_fs.h src/bsdiff.h src/bspatch.h $(OBJDIR)/__setup_obj_dir
	$(CPPC) $(FLAGS) ./src/fsarchive.cpp -c -o $@

$(OBJDIR)/bsdiff.o: src/bsdiff.c src/bsdiff.h $(OBJDIR)/__setup_obj_dir
	$(CC) $(FLAGS) ./src/bsdiff.c -c -o $@

$(OBJDIR)/settings.o: src/settings.cpp src/settings.h src/utils.h src/log.h $(OBJDIR)/__setup_obj_dir
	$(CPPC) $(FLAGS) ./src/settings.cpp -c -o $@

$(OBJDIR)/__setup_obj_dir :
	mkdir -p $(OBJDIR)
	touch $(OBJDIR)/__setup_obj_dir

.PHONY: clean bzip release

clean :
	rm -rf $(OBJDIR)/*.o
	rm -rf $(EXEC)

bzip :
	tar -cvf "$(DATE).$(EXEC).tar" $(SRCDIR)/* Makefile
	bzip2 "$(DATE).$(EXEC).tar"

release : FLAGS +=-O3 -D_RELEASE
release : $(EXEC)

