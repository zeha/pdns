
all: libyahttp.a


clean:
	-rm -f *.o *.a


libyahttp.a: reqresp.o router.o
	echo AR $@ $^
	$(AR) rcs $@ $^
