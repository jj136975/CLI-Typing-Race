NAME	=	tr_server

CC	=	gcc

SRC	=	src/server.c

DEF	=	# src/utils.c

OBJ	=	$(SRC:.c=.o)

DOBJ	=	$(DEF:.c=.o)

CFLAGS	=	-std=gnu17 -W -Wall -Wextra -I./include/ -I../common/include/

ROOT_DIR:=	$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

.PHONY	:	all clean fclean re

all	:	$(NAME)

$(NAME)	:	$(OBJ) $(DOBJ)
		$(CC) -o $(NAME) $(OBJ) $(DOBJ) $(LDFLAGS)
		cp $(NAME) ../

warning	:	CFLAGS += -Werror
warning	:	all

debug	:	CFLAGS += -g -DLOG_DEBUG -DDEBUG
debug	:	all

optimal	:	CFLAGS += -O2 -s
optimal	:	all

clean	:
		rm -f $(OBJ) $(DOBJ)

fclean	:	clean
		rm -f $(NAME)
		rm -f ../$(NAME)

re	:	fclean all
