#!/bin/bash

pglibdir=/usr/local/pgsql/lib/
mylibdir=/usr/local/lib/mysql/

pgincdir=/usr/local/pgsql/include/
myincdir=/usr/local/include/mysql/

pglibs="-lpq -lcrypto -lssl"
mylibs="-lmysqlclient -lcrypto"

echo This is the dbmail build script
echo I will have to ask you some questions about your system
echo ""
echo What database do you wish to use? Choices are \(m\)ysql and \(p\)ostgresql \>
read database

if [ $database = p ]; then
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

echo The library directory for $dbtype is now \[$libdir\]
echo The include directory for $dbtype is now \[$incdir\]
echo Do you wish to change this \(y/n\)?
read line

if [ $line = y ]; then
    echo Enter new library directory:
    read line
    libdir=$line
	
    echo Enter new include directory:
    read line
    incdir=$line
fi

echo The libraries are currently set to \[$libs\]. Do you want to change this?
read line

if [ $line = y ]; then
    echo Enter new library settings \(preceed each lib with \-l\):
    read libs
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
__INCDIR__=$incdir

EOF

cat Makefile.concept >>Makefile

echo ""
echo Done. You can now make dbmail by running \'make clean all\'.
echo Do you want this to be executed right now?
read line

if [ $line = y ]; then
    make clean all

    if [ $? -eq 0 ]; then
	echo ""
	echo Make succesfull. Do you want to install the binaries and man pages?
	read line

	if [ $line = y ]; then
	    echo binary directory is now $bindir. Do you want to change this?
	    read line

	    if [ $line = y ]; then
		echo Give new binary directory:
		read bindir
	    fi
	    
	    install.sh $bindir

	else
	    echo Note: You can install the files manually by running install.sh
	fi
    fi

fi


