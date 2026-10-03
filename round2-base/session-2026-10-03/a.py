def brentq(f,a,b):
    fa=f(a)
    for _ in range(200):
        m=(a+b)/2; fm=f(m)
        if (fm>0)==(fa>0): a,fa=m,fm
        else: b=m
    return (a+b)/2
P=lambda I:0.253-0.0973*I+0.0820*I*I
def loss_for(Io,Vin,Vo):
    IL=brentq(lambda IL: Vin*IL - Vo*Io - P(IL),0.1,30); return IL,P(IL)
for Io in [3.72,3.74,4.0,4.11,4.13,4.57,5.15,6.35]:
    IL,L=loss_for(Io,6,8.4); print(Io,round(IL,3),round(L,3),'eff',round(8.4*Io/(6*IL),4),'TJ',round(50+33*L,1), 'TJ excl ind',round(50+33*(L-IL*IL*0.0143),1) )
Io=brentq(lambda Io: loss_for(Io,6,8.4)[1]-75/33,1,6);print('rating',Io)
Io=brentq(lambda Io: (lambda IL,L: L-IL*IL*0.0143-75/33)(*loss_for(Io,6,8.4)),1,6);print('excl',Io, loss_for(Io,6,8.4))
print(3.72/6.35,4.11/6.35, 6.35-3.72,6.35-4.11, 1.94/6.35)
for IL in [7,8]:
    Io=(6*IL-P(IL))/8.4; print(IL,Io,P(IL),50+33*P(IL), 'eff',8.4*Io/(6*IL))
    Io=(6*IL-P(IL))/15; print('15V',Io,)
print(P(10), (6*10-P(10))/8.4)
print(brentq(lambda IL:(6*IL-P(IL))/8.4-6.35,1,20))
print('IL at 5.59 *4',5.59*4, 'nominal 6.35 out',)
print('---')
for Vin in (5.8,6.0,6.6):
    Io=brentq(lambda Io: loss_for(Io,Vin,8.4)[1]-75/33,1,6); print(Vin,Io)
# fit error sensitivity
Io1=brentq(lambda Io: loss_for(Io,6,8.4)[1]-(75/33+0.13),1,6); Io2=brentq(lambda Io: loss_for(Io,6,8.4)[1]-(75/33-0.13),1,6); print(Io1,Io2)
