
package audio.latency.low.ulltra;


import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.widget.Toast;
import android.util.Log;

public class UlltraService extends Service {
   
   Thread mainThread;

   @Override
   public IBinder onBind(Intent arg0) {
      return null;
   }

   @Override
   public int onStartCommand(Intent intent, int flags, int startId) {
      Toast.makeText(this, "Ulltra Service Started!", Toast.LENGTH_LONG).show();
		mainThread = new Thread(){
				public void run(){
				Log.d("Ulltra", "Thread run..");
					ulltraMain();
					Log.d("Ulltra", "Thread ending");
				}
			};
		mainThread.start();
		Log.d("Ulltra", "Thread started.");
      return START_STICKY;
   }
   
   @Override
   public void onDestroy() {
	  ulltraStop();
      Toast.makeText(this, "Ulltra Service Destroyed", Toast.LENGTH_LONG).show();
	  super.onDestroy();
   }

   public native int  ulltraMain();
   public native int  ulltraStop();

    static {
        System.loadLibrary("ulltra");
    }
}