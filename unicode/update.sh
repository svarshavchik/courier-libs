
filename="$1"
url="$2"

tmpfilename="$filename.tmp"

echo wget $url
wget -O - $url >$tmpfilename
if test ! -s $tmpfilename
then
	rm -f $tmpfilename
	exit 1
fi

if test -f $filename
then
	if cmp -s $filename $tmpfilename
	then
		rm -f $tmpfilename
		exit 0
	fi
fi
mkdir -p wwwarchive
test -f $filename && cp $filename wwwarchive
mv $tmpfilename $filename
