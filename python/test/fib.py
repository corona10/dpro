import time

def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

def test(*args):
    start = time.time()
    fib(30)
    print("%.6f sec" % (time.time() - start))

if __name__ == "__main__":
    test()
