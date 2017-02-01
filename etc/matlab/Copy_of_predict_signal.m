function [ pred ] = predict_signal( sig, msPred, realSig )
Fs = 48000;
nPred = Fs*msPred/1000;
frameSize = nPred*2; 
nFrames = 16;


sigL = conv(sig, ones(8,1)/(8),'same');
sigH = sig - sigL;

sig = sigL+sigH;

r = frameSize*(nFrames+1);
r = min(r, size(sig,1)-1);

recentSignal =  sig((end-r):(end-(frameSize)));
lastFrame = sig((end-frameSize):end);


lastFrameH = sigH((end-frameSize):end);
lastFrameL = sigL((end-frameSize):end);

recentSignalH =  sigH((end-r):(end-(frameSize*2)));
recentSignalL =  sigL((end-r):(end-(frameSize*2)));

[delayH, coeffsH] = find_delay([lastFrameH; zeros(size(recentSignalH,1)-frameSize-1,1)], recentSignalH);
[delayL, coeffsL] = find_delay([lastFrameL; zeros(size(recentSignalL,1)-frameSize-1,1)], recentSignalL);


subplot(2,1,1);
plot([coeffsL/max(coeffsL) coeffsH/max(coeffsH);zeros(frameSize,1) zeros(frameSize,1) ]);
subplot(2,1,2);

%predicted =  recentSignal(frameSize+(delay:(delay+nPred)));

predictedH =  recentSignalH(frameSize+(delayH:(delayH+nPred)));
predictedL =   recentSignalL(frameSize+(delayL:(delayL+nPred)));
predicted = predictedH + predictedL;
predicted = predictedL*3;

predicted = lastFrame(end:-1:(end-nPred+1))*0.8;

%predicted = -predicted;

hold off;
plot([sig((end-r):end); realSig(1:frameSize)]); % plot signal
hold on;
plot([zeros(size(recentSignal)); lastFrame], 'LineWidth',2);  % last frame

plot([zeros(delayH,1); lastFrameH; ]); % frame at found position
plot([zeros(delayL,1); lastFrameL; ]); % frame at found position

plot([ zeros(delayH,1); zeros(size(lastFrameH)); predictedH], 'LineWidth',2); % predicted at found
plot([ zeros(delayL,1); zeros(size(lastFrameL)); predictedL], 'LineWidth',2); % predicted at found


merged = [lastFrame; predicted];
%merged = medfilt1([merged],3);

fadeLen = nPred/1.5;

merged(end-fadeLen:end) = merged(end-fadeLen:end) .* cos(pi/2* (0:fadeLen)/(fadeLen))';
merged(end-fadeLen:end) = merged(end-fadeLen:end) + realSig(nPred-fadeLen:nPred) .*  cos(pi/2* (fadeLen:-1:0)/(fadeLen))';


merged = [merged; realSig(nPred:nPred*nFrames)];

plot([recentSignal; merged ], 'LineWidth',1); % merged


xlim([-Fs*msPred/1000 msPred*Fs/600]+r+msPred);
%plot([zeros(size(recentSignal)); zeros(size(lastFrame)); predicted ]); % predicted


 %figure; plot([recentSignal zeros(size(recentSignal)); merged merged]);
player = audioplayer([recentSignal; merged ], Fs);
playblocking(player);
end

