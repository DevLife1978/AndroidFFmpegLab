package app.jni.androidffmpeg;

import android.app.Activity;
import android.content.Context;
import android.content.CursorLoader;
import android.content.Intent;
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

import com.nostra13.universalimageloader.core.DisplayImageOptions;
import com.nostra13.universalimageloader.core.ImageLoader;
import com.nostra13.universalimageloader.core.ImageLoaderConfiguration;
import com.nostra13.universalimageloader.core.assist.ImageScaleType;
import com.nostra13.universalimageloader.core.display.FadeInBitmapDisplayer;
import com.nostra13.universalimageloader.core.listener.PauseOnScrollListener;

/**
 * Created by jayjay on 14. 12. 15..
 */
public class VideoActivity extends BaseActivity {

    public static final int REQUEST_MEDIA_LIST = 101;

    private GridView mGridView;
    private DisplayImageOptions options;
    private ImageLoader mImageLoader;
    private CustomAdapter customCursorAdapter;

    ffmpeg ffmpeg = new ffmpeg();

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

        mGridView = (GridView) findViewById(R.id.gridView);
        mGridView.setChoiceMode(GridView.CHOICE_MODE_MULTIPLE_MODAL);
        mGridView.setNumColumns(4);
        mGridView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                CustomAdapter.ViewHolder vh = (CustomAdapter.ViewHolder) view.getTag();
                ffmpeg.dump2log(vh.getVideoPath());
//                Intent result = new Intent();
//                result.putExtra("path", vh.getVideoPath());
//                setResult(Activity.RESULT_OK, result);
//                finish();
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
            long duration = ffmpeg.getDuration(path) + 5000;
            int hours, mins, secs, us;
            secs  = (int)(duration / 1000000);
            us    = (int)(duration % 1000000);
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            tv.setText(hours+":"+mins+":"+secs);
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
