
PROG=./testlogindomainlist
# PROG="valgrind --tool=memcheck --vgdb-error=1 ./testlogindomainlist"

cat >logindomainlist <<EOF
courier-mta.com:host.courier-mta.com:@
courier-mta.com:192.168.0.1:@
EOF

SERVER_ADDR=192.168.0.2 HTTP_HOST=host.courier-mta.com \
	   $PROG <logindomainlist
echo ""
SERVER_ADDR=192.168.0.1 HTTP_HOST=courier-mta.com \
	   $PROG <logindomainlist
echo ""

cat >logindomainlist <<EOF
*.com:mail.*.com:@
EOF

SERVER_ADDR=192.168.0.1 HTTP_HOST=mail.courier-mta.com \
	   $PROG <logindomainlist
echo ""

cat >logindomainlist <<EOF
courier-mta.com:host.courier-mta.com:gr
courier-mta.net:host.courier-mta.net:gr
courier-mta.org:host.courier-mta.org:gr
EOF
SERVER_ADDR=192.168.0.1 HTTP_HOST=host.courier-mta.com \
	   $PROG <logindomainlist
echo ""

rm -f logindomainlist
