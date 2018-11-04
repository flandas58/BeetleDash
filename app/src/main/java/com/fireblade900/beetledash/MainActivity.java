package com.fireblade900.beetledash;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.WindowManager;
import android.webkit.WebView;

public class MainActivity extends AppCompatActivity {

    private WebView bdWebView;

    @Override

    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        bdWebView = findViewById(R.id.bdWebView);
        bdWebView.getSettings().getJavaScriptEnabled();
        bdWebView.getSettings().setJavaScriptEnabled(true);

        bdWebView.loadUrl("http://192.168.4.1");
    }
}

