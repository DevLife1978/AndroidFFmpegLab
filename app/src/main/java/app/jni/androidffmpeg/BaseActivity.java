package app.jni.androidffmpeg;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;

/**
 * Created by jayjay on 14. 12. 15..
 */
public abstract class BaseActivity extends Activity {

    protected BaseActivityHandler mHandler;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mHandler = new BaseActivityHandler(this);
    }

    public abstract boolean handleMessage(BaseActivity activity, BaseActivityHandler handler, int what, Object value);

    public static class BaseActivityHandler extends Handler {
        private BaseActivity mActivity;
        public BaseActivityHandler(BaseActivity activity) {
            super();
            this.mActivity = activity;
        }

        @Override
        public void handleMessage(Message msg) {
            if (false == mActivity.handleMessage(mActivity, this, msg.what, msg.obj)) {
                super.handleMessage(msg);
            }
        }

        public void sendMessage(int what, Object obj) {
            Message msg = new Message();
            msg.what = what;
            msg.obj = obj;
            sendMessage(msg);
        }
    }
}