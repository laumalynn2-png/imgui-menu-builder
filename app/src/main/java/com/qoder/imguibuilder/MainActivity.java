package com.qoder.imguibuilder;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Bundle;
import android.view.Window;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class MainActivity extends Activity {

    static {
        System.loadLibrary("builder");
    }

    private static MainActivity instance;
    private static String savePath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN,
                android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        super.onCreate(savedInstanceState);

        instance = this;
        savePath = new File(getFilesDir(), "design.qib").getAbsolutePath();
        GLES3JNIView.nativeSetSavePath(savePath);

        GLES3JNIView view = new GLES3JNIView(this);
        setContentView(view);
    }

    public static void copyToClipboard(String text) {
        if (instance == null) return;
        ClipboardManager cm = (ClipboardManager) instance.getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText("ImGui Code", text));
        }
    }

    public static void saveContent(String content) {
        if (savePath == null) return;
        try {
            FileOutputStream fos = new FileOutputStream(savePath);
            fos.write(content.getBytes("UTF-8"));
            fos.close();
        } catch (IOException e) {
            // swallow
        }
    }

    public static String loadContent() {
        if (savePath == null) return "";
        try {
            FileInputStream fis = new FileInputStream(savePath);
            byte[] buf = new byte[(int) fis.available()];
            int read = 0;
            while (read < buf.length) {
                int n = fis.read(buf, read, buf.length - read);
                if (n < 0) break;
                read += n;
            }
            fis.close();
            return new String(buf, 0, read, "UTF-8");
        } catch (IOException e) {
            return "";
        }
    }
}
