include ../../../build.config

CFLAGS = $(DEFINES) $(INCLUDES)

CPPFILES = LogicMgrHost.cpp LogicMgrHostHw.cpp LogicMgrHostKey.cpp LogicMgrHostStatusPo.cpp LogicMgrHostSw.cpp LogicMgrHostSys.cpp LogicMgrUser.cpp

OBJ = $(CPPFILES:.cpp=.o)

all: $(OBJ)

.cpp.o:
	$(CC) $(CFLAGS) -c $(CPPFILES)

clean:
	rm -f $(OBJ)
