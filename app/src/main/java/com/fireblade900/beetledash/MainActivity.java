package com.fireblade900.beetledash;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.webkit.WebView;

public class MainActivity extends AppCompatActivity {

    private WebView bdWebView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        bdWebView = (WebView) findViewById(R.id.bdWebView);
        bdWebView.getSettings().getJavaScriptEnabled();
        bdWebView.loadUrl("http://192.168.1.128");
    }
}
