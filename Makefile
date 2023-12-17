.PHONY	:	server client

all	:	server client

debug	:	fclean
		make debug -C server/
		make debug -C client/

server	:
	make -C server/

client	:
	make -C client/

clean	:
	make clean -C client/
	make clean -C server/

fclean	:
	make fclean -C client/
	make fclean -C server/

re	:
	make re -C client/
	make re -C server/