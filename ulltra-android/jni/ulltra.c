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
 *
 */

#include <string.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

int s_ButtonPressCounter = 0;

extern int g_isRunning;

void ulltraServiceStart();
void ulltraServiceStop();

jstring
Java_audio_latency_low_ulltra_Flumine_stringFromJNI(JNIEnv* env,
	jobject thiz)
	
{	
	__android_log_print(ANDROID_LOG_DEBUG, "ulltra", "Damn button clicked %d times", s_ButtonPressCounter);
	if(s_ButtonPressCounter == 0)
		ulltraServiceStart();
	
	char szBuf[512];
	sprintf(szBuf, "You have pressed this huge button %d times (%d)!!!!", s_ButtonPressCounter++*s_ButtonPressCounter, g_isRunning);
	
	
	
	
	jstring str = (*env)->NewStringUTF(env, szBuf);
	return str;
}


jint
Java_audio_latency_low_ulltra_UlltraService_ulltraMain(JNIEnv* env,
	jobject thiz)
{
	ulltraServiceStart();
}



jint
Java_audio_latency_low_ulltra_UlltraService_ulltraStop(JNIEnv* env,
	jobject thiz)
{
	ulltraServiceStop();
}
