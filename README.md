# lip-sync-detect
## 2025高通边缘智能创新应用大赛入围决赛方案-智能终端赛道[https://qc-ieiot-challenge.aidlux.com/2025/]
## Finalist Proposal for the 2025 Qualcomm Edge Intelligence Innovation Application Competition - Smart Terminal Track


## 项目名称：基于lip-sync的视音频同步检测系统
## 项目详情：
1. 音画不同步是视音频相关领域最影响主观体验的，传统视音频同步检测使用的是在视频帧格式中增加私有描述字段，通过特定仪器进行检测，判断电视或者新媒体视音频中视音频是否同步，测试条件较高，并且例如电视台、广电网络公司需要在星期二停台时间才能完成。
2. 本应用旨在通过视音频播放的时候使用AI技术对主持人或者电影电视剧节目播放过程中主持人、演员嘴型lip的分析与实际声纹进行AI分析实现一种快速可实施的算法实现，并可落地实现。
3. 本项目使用视频中人脸提取，然后提取人脸关键特征点或人脸mesh获得嘴唇所在适量位置，判断人嘴张开关闭程度，以判断说话声音数值大小趋势，并对当前视音频的音频部分声场响度大小提取。对人嘴大小和声场响度进行关联，判断二者延时程度，即可完成视音频不同步时间检测。
4. 项目实施出发点：需要人脸68个特征点的AI模型基本上CPU即可满足（如果需要用68个特征点去做人识别可能需要GPU或者NPU），因此CPU即可，对比传统intel-13900等CPU，他的主频完全利用不到，浪费cpu资源，因此选择了本边缘模块，可实现高效的边缘推理场景。也尝试使用了高通的model-zoo的face-mesh，但是前期已经在C++环境现行实现了代码部分，因此直接使用了高通开发环境进行代码部署。


## Project Name: lip sync based video and audio synchronization detection system
## Project details:
1. The lack of synchronization between audio and video is the most subjective experience in the field of video and audio. Traditional video and audio synchronization detection uses private description fields added to the video frame format, which are detected through specific instruments to determine whether the TV or new media video and audio are synchronized. The testing conditions are relatively high, and for example, TV stations and broadcasting network companies need to stop broadcasting on Tuesdays to complete it.
2. This application aims to use AI technology during video and audio playback to analyze the lip movements of hosts or actors' mouths and actual voiceprints during the playback of movies and TV shows. It is a fast and feasible algorithm implementation that can be implemented on the ground.
3. This project uses face extraction from videos, and then extracts key facial feature points or facial meshes to obtain the appropriate position of the lips, judge the degree of mouth opening and closing, to determine the trend of speaking sound values, and extract the loudness of the audio part of the current video and audio. By correlating the size of the human mouth with the volume of the sound field, and determining the degree of delay between the two, the detection of video and audio asynchronous time can be completed.

## Coding info
the coding need dlib envirment in Quallmomm environment, we can use the following command to build it:
1. git clone https://github.com/davisking/dlib.git
2. cd dlib
3. mkdir build
4. cmake .. -DCMAKE_BUILD_TYPE=Release -DDLIB_USE_BLAS=OFF -DDLIB_USE_LAPACK=OFF
5. make -j8
6. sudo make install

## Coding evacuation results
Test for video have 2000ms-audio-delay and so on.
<img width="1920" height="1080" alt="Screenshot from 2025-09-16 12-18-18" src="https://github.com/user-attachments/assets/6c1b0737-c531-4e3a-baa3-5913c35f14c0" />
<img width="2806" height="1592" alt="image" src="https://github.com/user-attachments/assets/bb8e8ec7-c6be-4351-b777-d25612b206e2" />
<img width="2804" height="1594" alt="image" src="https://github.com/user-attachments/assets/775a4dd3-b1f6-4a0f-9981-c032b900accc" />

If we want create the delayed-video,we can use ffmpeg command "ffmpeg -i 1.ts -af "adelay=500|500" -vcodec copy delay500.mp4" to create it.

# Thanks the 2025 Qualcomm Edge Intelligence Innovation Application Competition, Aidlux，Quectel ，AidLux_Me（阿加犀小助手）and so on。
