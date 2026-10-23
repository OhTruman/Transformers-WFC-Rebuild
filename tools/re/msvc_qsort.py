"""Exact port of the WFC binary's CRT qsort (0x830EF590, MSVC qsort.c: CUTOFF 8, median of three, equal-skip),
used by HmArraySorter.SortArray (0x82CD2490: SortedIndices = 0..n-1, then qsort with the script delegate)."""

def qsort(a, comp):
    n = len(a)
    if n < 2: return a
    def swap(i, j): a[i], a[j] = a[j], a[i]
    lostk, histk = [], []
    lo, hi = 0, n - 1
    while True:
        size = hi - lo + 1
        if size <= 8:
            h = hi
            while h > lo:                       # shortsort: move the max to the end
                m = lo
                for p in range(lo + 1, h + 1):
                    if comp(a[p], a[m]) > 0: m = p
                swap(m, h); h -= 1
        else:
            mid = lo + size // 2
            if comp(a[lo], a[mid]) > 0: swap(lo, mid)
            if comp(a[lo], a[hi]) > 0: swap(lo, hi)
            if comp(a[mid], a[hi]) > 0: swap(mid, hi)
            loguy, higuy = lo, hi
            while True:
                if mid > loguy:
                    loguy += 1
                    while loguy < mid and comp(a[loguy], a[mid]) <= 0: loguy += 1
                if mid <= loguy:
                    loguy += 1
                    while loguy <= hi and comp(a[loguy], a[mid]) <= 0: loguy += 1
                higuy -= 1
                while higuy > mid and comp(a[higuy], a[mid]) > 0: higuy -= 1
                if higuy < loguy: break
                swap(loguy, higuy)
                if mid == higuy: mid = loguy
            higuy += 1
            if mid < higuy:
                higuy -= 1
                while higuy > mid and comp(a[higuy], a[mid]) == 0: higuy -= 1
            if mid >= higuy:
                higuy -= 1
                while higuy > lo and comp(a[higuy], a[mid]) == 0: higuy -= 1
            if higuy - lo >= hi - loguy:
                if lo < higuy: lostk.append(lo); histk.append(higuy)
                if loguy < hi: lo = loguy; continue
            else:
                if loguy < hi: lostk.append(loguy); histk.append(hi)
                if lo < higuy: hi = higuy; continue
        if not lostk: return a
        lo, hi = lostk.pop(), histk.pop()
