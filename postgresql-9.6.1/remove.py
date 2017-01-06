import os

os.system("rm -r /usr/local/pgsql/data")
os.system("mkdir /usr/local/pgsql/data")
os.system("chown postgres /usr/local/pgsql/data")
