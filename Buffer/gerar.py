import sys
import os
import random
def main():
	count = 0
	for c in xrange(1,100):
		name = "testes/workload"+str(c)+".txt"
		file = open(name,"w")
		size_pages = 32
		texto = "32\n"
		for i in xrange(1,100):
			page = random.randint(0,31)
			texto += str(page) + " "
			op = random.randint(0,10)
			if op >= 6:
				texto+= "w"
			else:
				texto+= "r"
			texto+="\n"
		file.write(texto)
		file.close()
		count+=os.system("./buffer "+name)
		if count != 0:
			print("erro em "+name+ " \n")
			break
		print("\n\n\n")
if __name__ == "__main__":
	main()