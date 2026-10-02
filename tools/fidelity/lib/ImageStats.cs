// Image statistics for the fidelity scripts (compiled by Add-Type). Works on 24/32-bit bitmaps.
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static class WfcImage {
    // Downsampled luminance grid (cell x cell blocks), 0..255.
    public static float[] Luma(string path, int cell) {
        using (var bmp = new Bitmap(path)) {
            int w = bmp.Width / cell, h = bmp.Height / cell;
            var outv = new float[w * h + 2];
            outv[0] = w; outv[1] = h;
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                int stride = data.Stride;
                var row = new byte[stride];
                var acc = new double[w * h];
                for (int y = 0; y < h * cell; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * stride, row, 0, stride);
                    int cy = y / cell;
                    for (int x = 0; x < w * cell; x++) {
                        int o = x * 3;
                        acc[cy * w + x / cell] += 0.114 * row[o] + 0.587 * row[o + 1] + 0.299 * row[o + 2];
                    }
                }
                for (int i = 0; i < w * h; i++) outv[i + 2] = (float)(acc[i] / (cell * cell));
            } finally { bmp.UnlockBits(data); }
            return outv;
        }
    }
    // Mean absolute luminance difference of two grids, and the fraction of cells changing > thr.
    public static double[] Diff(float[] a, float[] b, float thr) {
        int n = Math.Min(a.Length, b.Length);
        double s = 0; int changed = 0;
        for (int i = 2; i < n; i++) { float d = Math.Abs(a[i] - b[i]); s += d; if (d > thr) changed++; }
        return new double[] { s / (n - 2), (double)changed / (n - 2) };
    }
    // Count pixels that match a saturated debug-overlay colour (unlit GL_LINES: pure green / cyan /
    // yellow / magenta as drawn by World::draw's debug overlay) - tolerance tol per channel.
    public static int DebugPixels(string path, int tol) {
        int[][] cols = { new[] {77, 255, 102}, new[] {51, 204, 255}, new[] {255, 255, 51}, new[] {255, 77, 255} };
        int hits = 0;
        using (var bmp = new Bitmap(path)) {
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                var row = new byte[data.Stride];
                for (int y = 0; y < bmp.Height; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, data.Stride);
                    for (int x = 0; x < bmp.Width; x++) {
                        int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
                        foreach (var c in cols)
                            if (Math.Abs(r - c[0]) <= tol && Math.Abs(g - c[1]) <= tol && Math.Abs(b - c[2]) <= tol) { hits++; break; }
                    }
                }
            } finally { bmp.UnlockBits(data); }
        }
        return hits;
    }
}
