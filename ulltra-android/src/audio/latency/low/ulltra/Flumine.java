/*
 * Created by VisualGDB. Based on hello-jni example.
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package audio.latency.low.ulltra;

import android.app.Activity;
import android.view.View;
import android.widget.Button;
import android.os.Bundle;
import android.util.Log;
import android.content.Intent;
 
public class Flumine extends Activity
{
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        /* Create a Button and set its content.
         * the text is retrieved by calling a native
         * function.
         */
        final Button  button = new Button(this);
		button.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				//button.setText( stringFromJNI());
				Log.d("Ulltra", "Button clicked!");
				//startService(v);
				startService(
				new Intent(
				getBaseContext(),
				 UlltraService.class));
			}
		});
        setContentView(button);
    }


   // Method to stop the service
   public void stopService(View view) {
      stopService(new Intent(getBaseContext(), UlltraService.class));
   }

	

}
