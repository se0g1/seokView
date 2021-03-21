# seokView

seokView는 iOS introsepction tool로 정적 분석 도구로, iOS 13까지 tfp0를 통해 1Day 분석 및 정적 분석을 진행할 수 있습니다.
따라서 기기에 상관없이 탈옥이 가능한 디바이스(CheckRa1n)는 seokView를 사용할 수 있습니다. 
지원 되는 버전에 존재하지 않을 경우 연락주시면 요청한 버전에 맞춰 빌드하여 보내드리겠습니다. 테스트 환경은 iPhoneSE(iOS 13.6) 입니다.  
궁금한 점 및 사용이 되지 않을 경우  wooseook@gmail.com 또는 https://www.facebook.com/profile.php?id=100004230373093 로 페이스북 메시지 부탁드리겠습니다.  
  사용된 코드는 @Bazad가 공개한 도구의 코드를 참고하여 제작되었고, @cdpython의 도움을 받고 기능을 만들었습니다. 


지원 되는 버전은 다음과 같습니다. 해당 버전은 ipsw.me 또는 iOS Model List에서 확인할 수 있습니다.  
1. iPhone8,4_17G68  
2. iPhone10,1_16C101  
3. iPhone10,1_16G77  
4. iPhone10,6_16E227  
5. iPhone10,4_17A860  

추가될 기능  
1. zone all print  
2. zone space print  
3. find port  
4.  ...


### 사용법
1. 지원되는 버전에 존재할 경우  
 -> kernel/kernel_parameters.c 코드 내부 주소 변경  
 -> kernel/kernel_call_parameters.c 코드 내부 주소 변경  
 -> ktrr/ktrr_bypass_parameters.c 코드 내부 주소 변경  
 -> BUILD  
2. 지원되는 버전에 존재하지 않을 경우  
 -> 사용하고자하는 버전 혹은 iPhone 종류를 위의 메일, 페이스북 메시지를 보내주세요  

빌드 후 탈옥된 디바이스를 ssh로 접근하여 kernel_symbol 폴더와 seokView 바이너리를 업로드  

---

### 구현 완료

0. Function
```
se0g1> ?
i                     Print system information
r <address> [length]  Read and print formatted memory
```

1. System Information
```
iPhone:~ root# ./seokView
[+] Platform: iPhone8,4 17G68
[+] task_for_pid(0) = 0x907
[+] Kernel Base is 0xfffffff00b734000
[+] KASLR slide is 0x4730000
se0g1> i
[+] release:		 iPhone8,4 17G68
[+] machine: 		 root:xnu-6153.142.1~4/RELEASE_ARM64_S8000
[+] cpu type: 	 	 0x100000c(arm64)
[+] cpu subtype: 	 0x1(arm64 v8)
[+] cpus: 		 02 cores / 2 threads
[+] memory:		 0x7db80000(2.0G)
[+] page size: 	 	 0x4000(16K)
```

2. Kernel Address Read
```
se0g1> r 0xfffffff00b734000
fffffff00b734000:  0100000cfeedfacf
se0g1> r 0xfffffff00b734000 16
fffffff00b734000:  0100000cfeedfacf 00000ed800000016
```



---
[ 추후수정 ]  
1. 문제점 : kernel_symbols 폴더(symbole 파일)와 바이너리를 함께 업로드  
 해결법 : 전역 주소로 코드 선언해서 불러오게 끔 수정 필요 
2. 문제점 : /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS13.2.sdk/usr/include/mach/mach_vm.h 코드내부에서 주석 처리함 -> #error mach_vm.h unsupported. 
3. 문제점 : write 이후에 f기능 추가를 위해서는 kernel.c 등 많은 함수들을 추가해야함, 
필요한 코드를 추가하는 식으로 진행해야하지만, write 코드 추가 이후 파일 붙어넣기로 인해 코드 중보해결이 필요, ( memctl / ktrw 비교하면서 수정 -> memctl 기능들을 참고가 대부분  )
