package com.example.testapplication;

import android.os.Bundle;
import android.os.Handler;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.util.Log;
import android.view.View;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.TextView;

import java.util.logging.Level;
import java.util.logging.Logger;

public class MainActivity extends AppCompatActivity {

    public static String TAG = "FlowTrace";
    public static Logger LOGGER  = Logger.getLogger(MainActivity.class.getName());
    int dummy;
    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    private final Handler mHandler = new Handler();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Toolbar toolbar = (Toolbar) findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        Log.d(TAG, "onCreate - d1");
        Log.d(TAG, "onCreate - d2", new Exception("exception thrown"));
        Log.i(TAG, "onCreate - i1");
        Log.i(TAG, "onCreate - i2", new Exception("exception thrown"));
        Log.w(TAG, "onCreate - w1");
        Log.w(TAG, "onCreate - w2", new Exception("exception thrown"));
        Log.v(TAG, "onCreate - v1");
        Log.v(TAG, "onCreate - v2", new Exception("exception thrown"));
        Log.e(TAG, "onCreate - e1");
        Log.e(TAG, "onCreate - e2", new Exception("exception thrown"));
        Log.println(4, TAG, "onCreate - p1");
        LOGGER.log(Level.INFO, "onCreate - LOGGER msg 1");
        LOGGER.log(Level.INFO, "onCreate - LOGGER msg 2", new Exception("LOGGER exception thrown"));

        FloatingActionButton fab = (FloatingActionButton) findViewById(R.id.fab);
        fab.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {

                dummy++;
                doDummy_2();
                doDummy();
                Log.d(TAG, "test Flowtrce1");
                Log.d(TAG, stringFromJNI());
                System.loadLibrary("flowtrace");
                Snackbar.make(view, "Replace with your own action", Snackbar.LENGTH_LONG).setAction("Action", null).show();

            }
        });

        TextView tv = (TextView) findViewById(R.id.sample_text);
        tv.setText(stringFromJNI());
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_settings) {
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    void doDummy()
    {
        dummy++;
        Log.e(TAG, "test error");
        Log.e(null, null, new Exception("exception thrown"));
        doDummy_2();
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                Log.d(TAG, "In handler");
            }
        });
    }

    void doDummy_2()
    {
        //test empty method
        dummy++;
    }
    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
}
