/*��ͷ���б�Ҫ�������ڵ��õ�ʱ��֪������Щ����*/
#ifdef BUILD_DLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __declspec(dllimport)
#endif
EXPORT void export(void);
