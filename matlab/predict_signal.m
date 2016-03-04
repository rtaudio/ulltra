function [ pred ] = predict_signal( sig, msPred, realSig )
Fs = 48000;
nPred = Fs*msPred/1000;
frameSize = nPred*2; 
nFrames = 18;


sigL = conv(sig, ones(Fs*0.001,1)/(Fs*0.001),'same');
sigH = sig - sigL;

sig = sigL+sigH;

r = frameSize*(nFrames+1);
r = min(r, size(sig,1)-1);

recentSignal =  sig((end-r):(end-(frameSize)));
lastFrame = sig((end-frameSize):end);
recentSignal2 =  sig((end-r):(end-(frameSize*2)));
[delay, coeffs] = find_delay([lastFrame; zeros(size(recentSignal2,1)-frameSize-1,1)], recentSignal2);


subplot(2,1,1);
plot([(coeffs);zeros(frameSize,1)]);
coeffs(delay)
subplot(2,1,2);

predicted =  recentSignal(frameSize+(delay:(delay+nPred)));

hold off;
plot([sig((end-r):end); realSig(1:frameSize)]); % plot signal
hold on;
plot([zeros(size(recentSignal)); lastFrame], 'LineWidth',3);  % last frame
plot([zeros(delay,1); lastFrame; ]); % frame at found position
plot([ zeros(delay,1); zeros(size(lastFrame)); predicted]); % predicted at found

if sign(lastFrame(end-5)) ~= sign(predicted(5))
    predicted = -predicted;
end

merged = [lastFrame; predicted];
merged = medfilt1([merged],3);

fadeLen = nPred/1.5;

merged(end-fadeLen:end) = merged(end-fadeLen:end) .* cos(pi/2* (0:fadeLen)/(fadeLen))';

merged(end-fadeLen:end) = merged(end-fadeLen:end) + realSig(nPred-fadeLen:nPred) .*  cos(pi/2* (fadeLen:-1:0)/(fadeLen))';

merged = [merged; realSig(nPred:nPred*nFrames)];

plot([zeros(size(recentSignal)); merged ], 'LineWidth',1.2); % merged


xlim([-Fs*msPred/1000 msPred*Fs/800]+r+msPred);
%plot([zeros(size(recentSignal)); zeros(size(lastFrame)); predicted ]); % predicted



player = audioplayer([recentSignal; merged], Fs);
playblocking(player);
end

