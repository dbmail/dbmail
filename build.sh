#!/bin/bash

pglibdir=/usr/local/pgsql/lib/
mylibdir=/usr/local/lib/mysql/

pgincdir=/usr/local/pgsql/include/
myincdir=/usr/include/mysql/

pglibs="-lpq -lcrypto -lssl -lz"
mylibs="-lmysqlclient -lcrypto -lz"

bindir=/usr/local/sbin

echo This is the dbmail build script
echo I will have to ask you some questions about your system
echo ""

if [ "$1" != "" ]; then
    database=$1
else
    echo What database do you wish to use? Choices are \(m\)ysql and \(p\)ostgresql \>
    read database
fi

if [ "$database" = p ]; then
    echo You have selected PostgreSQL as database
    libdir=$pglibdir
    incdir=$pgincdir
    libs=$pglibs
    dbtype=PostgreSQL
    db=pgsql
else
    echo You have selected MySQL as database
    libdir=$mylibdir
    incdir=$myincdir
    libs=$mylibs
    dbtype=MySQL
    db=mysql
fi

echo The library directory for $dbtype is now \[$libdir\]. 
echo Enter new directory or press RETURN to keep this setting:
read line
if [ "$line" != "" ]; then
    libdir=$line
fi

echo The include directory for $dbtype is now \[$incdir\]. 
echo Enter new directory or press RETURN to keep this setting:
read line
if [ "$line" != "" ]; then	
    incdir=$line
fi


echo The libraries are currently set to \[$libs\]. 
echo Enter new libraries \(preceed each by \-l\) or press RETURN to keep this setting:
read line
if [ "$line" != "" ]; then
    libs=$line
fi

# This is poor, but there's no easy way to check for a header file
# (that's what the autoconf system is for)
if [ -f /usr/include/endian.h ] || [ -f "${incdir}/endian.h" ]; then
    cflags="$cflags -DHAVE_ENDIAN_H"
fi
if [ -f /usr/include/crypt.h ] || [ -f "${incdir}/crypt.h" ]; then
    cflags="$cflags -DHAVE_CRYPT_H"
fi

echo ""
echo Creating makefile..

cat >Makefile <<EOF
#!/bin/bash

#
# Auto-generated Makefile for $dbtype
#

__DBTYPE__=$db
__LIBS__=$libs
__LIBDIR__=$libdir
__INCDIR__=$incdir -I.
__CFLAGS__=$cflags

EOF

cat Makefile.concept >>Makefile

echo ""
echo Done. You can now make dbmail by running \'make clean all\'.
echo Do you want this to be executed right now?
read line

if [ "$line" = y ]; then
    make clean all

    if [ $? -eq 0 ]; then
	echo ""
	echo Make succesfull. Do you want to install the binaries and man pages?
	read line

	if [ "$line" = y ]; then
	    echo Target binary directory is now $bindir. 
	    echo Enter new directory or press RETURN to keep this setting:
	    read line

	    if [ "$line" != "" ]; then
		bindir=$line
	    fi
	    
	    install-dbmail.sh $bindir
	    if [ $? -ne 0 ]; then
		echo install script failed. You can rerun this manually by running install-dbmail.sh
		exit 1
	    fi

	else
	    echo Note: You can install the files manually by running install-dbmail.sh
	fi
    else
	echo Sorry, Make failed. 
	echo Please report any problems to the mailing list: dbmail@dbmail.org
	echo You can find more information \(including how to subscribe to the mailinglist\) on www.dbmail.org
    fi

fi


