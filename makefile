# Uncomment the cc line for your version of Unix. If I knew how to use
# Configure to create a makefile I would but I don't so you'll have to 
# figure it out for yourselves. Aww , poor bunnies :)

CC=PLEASE SET COMPILE OPTION IN MAKEFILE

# HP-UX ver 10.01
#CC=cc -Ae -DNO_USLEEP

# HP-UX above 10.01
#CC=cc -Ae 

# Linux
#CC=cc -DLINUX 

# Solaris using BSD compiler. This is the preferred option for Solaris as 
# it has the usleep() function.
#CC=/usr/ucb/cc -DSUN_BSD_BUG -lsocket -lnsl

# Solaris using main compiler. Only use this if you don't have the
# BSD compiler installed as it doesn't support usleep().
#CC=cc -DNO_USLEEP -lsocket -lnsl

# FreeBSD
#CC=cc -DFREEBSD

# FreeBSD with alternative crpyt libraries
#CC=cc -DFREEBSD -lcrypt_i

# FreeBSD without any crypt libraries
#CC=cc -DFREEBSD -DNO_CRYPT

# Dynix?
#CC=cc -lsocket -lnsl

# OSF, AIX or anything else I don't know about.
#CC=cc

avios: avios151.c avios151.h makefile
	$(CC) avios151.c -o avios
