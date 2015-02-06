package app.jni.androidffmpeg;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.CursorLoader;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore.Video.Media;
import android.provider.MediaStore.Video.Thumbnails;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.SimpleCursorAdapter;
import android.widget.TextView;
import android.widget.Toast;

import com.loopj.android.http.AsyncHttpClient;
import com.loopj.android.http.AsyncHttpResponseHandler;
import com.loopj.android.http.RequestParams;
import com.loopj.android.http.ResponseHandlerInterface;
import com.nostra13.universalimageloader.core.DisplayImageOptions;
import com.nostra13.universalimageloader.core.ImageLoader;
import com.nostra13.universalimageloader.core.ImageLoaderConfiguration;
import com.nostra13.universalimageloader.core.assist.ImageScaleType;
import com.nostra13.universalimageloader.core.display.FadeInBitmapDisplayer;
import com.nostra13.universalimageloader.core.listener.PauseOnScrollListener;

import org.apache.http.Header;
import org.apache.http.HttpEntity;
import org.apache.http.HttpResponse;
import org.apache.http.client.HttpClient;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.mime.FormBodyPart;
import org.apache.http.entity.mime.FormBodyPartBuilder;
import org.apache.http.entity.mime.HttpMultipartMode;
import org.apache.http.entity.mime.MultipartEntity;
import org.apache.http.entity.mime.MultipartEntityBuilder;
import org.apache.http.entity.mime.content.ContentBody;
import org.apache.http.entity.mime.content.StringBody;
import org.apache.http.impl.client.DefaultHttpClient;
import org.apache.http.protocol.BasicHttpContext;
import org.apache.http.protocol.HttpContext;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URL;

import app.jni.ffmpegandroid.ffmpeglib;

/**
 * Created by jayjay on 14. 12. 15..
 */
public class VideoActivity extends BaseActivity {

    public static final int REQUEST_MEDIA_LIST = 101;

    private GridView mGridView;
    private DisplayImageOptions options;
    private ImageLoader mImageLoader;
    private CustomAdapter customCursorAdapter;

    ffmpeglib ffmpeglib = new ffmpeglib();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 이미지 로더
        options = new DisplayImageOptions.Builder()
                .resetViewBeforeLoading(true)
                .cacheInMemory(true)
                .cacheOnDisc(true)
                .imageScaleType(ImageScaleType.EXACTLY)
                .bitmapConfig(Bitmap.Config.RGB_565)
                .displayer(new FadeInBitmapDisplayer(200))
                .build();


        ImageLoaderConfiguration config = new ImageLoaderConfiguration.Builder(getApplicationContext())
                .defaultDisplayImageOptions(options)
                .build();
        ImageLoader.getInstance().init(config);

        setContentView(R.layout.activity_record);

        findViewById(R.id.stop).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        ffmpeglib.stop();
                    }
                }).start();
            }
        });

        mGridView = (GridView) findViewById(R.id.gridView);
        mGridView.setChoiceMode(GridView.CHOICE_MODE_MULTIPLE_MODAL);
        mGridView.setNumColumns(4);
        mGridView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {

                final String filename = "/sdcard/Movies/output.mp4";
                final File file = new File(filename);
                if (file.exists()) {
                    file.delete();
                }

                final CustomAdapter.ViewHolder vh = (CustomAdapter.ViewHolder) view.getTag();
                Log.i("Video", vh.getVideoPath());
                final int total_frames = ffmpeglib.media_total_frame(vh.getVideoPath());
                final ProgressDialog progressDialog = new ProgressDialog(VideoActivity.this);
                progressDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
                progressDialog.setTitle("Transcoding...");
                progressDialog.setMax(total_frames);
                progressDialog.show();
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        long time = System.currentTimeMillis();
                        ffmpeglib.setCallback(new ffmpeglib.FFmpegLibraryCallback() {
                            @Override
                            public void onDecodedTime(int frame, int total_frame) {
                                Log.i("Transcoding", frame + " / " + total_frame);
                                progressDialog.setMax(total_frame);
                                progressDialog.setProgress(frame);
                            }

                            @Override
                            public void onFinished(int ret, String error) {
                                Log.i("Transcoding", "Finished : " + error);
                                progressDialog.dismiss();
                            }


                        });
                        ffmpeglib.ffmpeg_test(vh.getVideoPath(), filename);
                        long duration = ffmpeglib.media_length(vh.getVideoPath()) + 5000;
                        int hours, mins, secs, us;
                        secs  = (int)(duration / 1000000);
                        us    = (int)(duration % 1000000);
                        mins  = secs / 60;
                        secs %= 60;
                        hours = mins / 60;
                        mins %= 60;

                        final StringBuilder sb = new StringBuilder();
                        sb.append("Original " + ((int)(duration / 1000000)));
                        sb.append("\n");
                        File input_file = new File(vh.getVideoPath());
                        if(null != input_file) {
                            sb.append("input length -> " + (int) (input_file.length() / 1024.0 / 1024.0) + "M");
                            sb.append('\n');
                        }
                        sb.append("Test " + (System.currentTimeMillis() - time) / 1000 + " min");
                        sb.append('\n');
                        File output_file = new File(filename);
                        if(null != output_file) {
                            sb.append("output length -> " + (int)(output_file.length() / 1024.0 / 1024.0 ) + "M");
                        }

                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                AlertDialog.Builder alert = new AlertDialog.Builder(VideoActivity.this);
                                alert.setTitle("Transcoding result");
                                alert.setMessage(sb.toString());
                                alert.show();
                            }
                        });

                    }
                }).start();
//                ffmpeglib.dump2log(vh.getVideoPath());
//
//
//                ffmpeglib.runScaleMedia(vh.getVideoPath(), "/storage/emulated/0/DCIM/Camera/output.mp4");
            }
        });
        findViewById(R.id.open_video).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mHandler.sendMessage(REQUEST_MEDIA_LIST, null);
            }
        });

        mImageLoader = ImageLoader.getInstance();

        mGridView.setOnScrollListener(new PauseOnScrollListener(mImageLoader, false, true));

        String[] proj = { Media._ID,
                Media.DATA,
                Media.DISPLAY_NAME,
                Media.SIZE ,
                Media.TITLE,
                Media.DATE_ADDED
        };

        CursorLoader cursorLoader = new CursorLoader(
                this,
                Media.EXTERNAL_CONTENT_URI,
                proj,
                null,
                null,
                Media.DATE_ADDED + " DESC");

        Cursor cursor = cursorLoader.loadInBackground();
        Log.d(getClass().getName(), cursor.getCount() + "");
        String[] from = {};
        int[] to = {};
        customCursorAdapter = new CustomAdapter(
                this,
                R.layout.view_board_photo_item,
                cursor,
                from,
                to,
                0);
        mGridView.setAdapter(customCursorAdapter);
    }

    @Override
    public boolean handleMessage(BaseActivity activity, BaseActivityHandler handler, int what, Object value) {
        switch (what) {
            case REQUEST_MEDIA_LIST:
            {

            }
                return true;
            default:
                return false;
        }
    }

    public class CustomAdapter extends SimpleCursorAdapter {

        private LayoutInflater mLayoutInflater;
        private Context context;
        private int layout;
        private boolean mSelect = false;

        private class ViewHolder {
            ImageView iv_photo;
            ImageView iv_select;
            private String mVideoPath = null;

            ViewHolder(View v) {
                iv_photo = (ImageView)v.findViewById(R.id.iv_photo);
                iv_select = (ImageView)v.findViewById(R.id.iv_select);
            }
            public String getVideoPath() {
                return mVideoPath;
            }

            public void setVideoPath(String mVideoPath) {
                this.mVideoPath = mVideoPath;
            }
        }

        public CustomAdapter (Context ctx, int layout, Cursor c, String[] from, int[] to, int flags) {
            super(ctx, layout, c, from, to, flags);
            this.context = ctx;
            this.layout = layout;
            mLayoutInflater = LayoutInflater.from(ctx);
        }

        @Override
        public View newView(Context ctx, Cursor cursor, ViewGroup parent) {
            View view = mLayoutInflater.inflate(layout, parent, false);
            ViewHolder vh = new ViewHolder(view);
            view.setTag(vh);
            return view;
        }

        @Override
        public void bindView(View v, Context ctx, Cursor c) {
            int position = c.getPosition();
            ViewHolder vh = (ViewHolder) v.getTag();
            TextView tv = (TextView) v.findViewById(R.id.duration);
            // 파일 경로
            String path = c.getString(c.getColumnIndex(Media.DATA));
            vh.setVideoPath(path);
            long duration = ffmpeglib.media_length(path) + 5000;
            int hours, mins, secs, us;
            secs  = (int)(duration / 1000000);
            us    = (int)(duration % 1000000);
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            tv.setText((10 > hours ? "0"+hours : hours)+":" + ( 10 > mins ? "0" + mins : mins ) + ":" + ( 10 > secs ? "0" : "") + secs);
            int id = c.getInt(c.getColumnIndex(Media._ID));

            path = "file://"+getVideoThumbnail(id);
            mImageLoader.displayImage(path, vh.iv_photo);
        }

        private String getVideoThumbnail(int id) {
            final String thumb_DATA = Thumbnails.DATA;
            final String thumb_VIDEO_ID = Thumbnails.VIDEO_ID;
            Uri uri = Thumbnails.EXTERNAL_CONTENT_URI;
            String[] projection = {thumb_DATA, thumb_VIDEO_ID};
            String selection = thumb_VIDEO_ID + "=" + id + " AND " + Thumbnails.KIND + "=" + Thumbnails.MINI_KIND;
            Cursor thumbCursor = getContentResolver().query(uri, projection, selection, null, null);
            String thumbPath = null;
            //  Bitmap thumbBitmap = null;
            if (thumbCursor.moveToFirst()) {

                int thCulumnIndex = thumbCursor.getColumnIndex(thumb_DATA);

                thumbPath = thumbCursor.getString(thCulumnIndex);
            }
            return thumbPath;
        }

        public boolean isSelect() {
            return mSelect;
        }

        public void isSelect(boolean select) {
            mSelect = select;
        }
    }
}
