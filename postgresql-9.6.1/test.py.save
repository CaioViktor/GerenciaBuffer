import os
os.system("/usr/local/pgsql/bin/initdb -D /usr/local/pgsql/data")
os.system("/usr/local/pgsql/bin/postgres -D /usr/local/pgsql/data >logfile 2>&1 &")
os.system("/usr/local/pgsql/bin/createdb test")
os.system("/usr/local/pgsql/bin/pgbench -i test -s 50")
