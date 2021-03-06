#include "handlersmanager.h"
#include "include/base/cef_bind.h"
#include "include/wrapper/cef_closure_task.h"
#include "multithreading.h"
#include <functional>

using namespace std::placeholders;

void HandlersManager::Init1(CefRefPtr<MainHandler> Handler,std::function<void(const std::string&)> SendTextResponceCallback,std::function<void(const std::string&, int)> UrlLoadedCallback,std::function<void()> LoadSuccessCallback,std::function<void(char*,int,int)> PaintCallback, std::function<void(int64)> OldestRequestTimeChangedCallback)
{
    this->Handler.swap(Handler);
    this->SendTextResponceCallback = SendTextResponceCallback;
    this->UrlLoadedCallback = UrlLoadedCallback;
    this->LoadSuccessCallback = LoadSuccessCallback;
    this->PaintCallback = PaintCallback;
    this->OldestRequestTimeChangedCallback = OldestRequestTimeChangedCallback;

    this->Handler->EventLoadSuccess.push_back(std::bind(&HandlersManager::LoadSuccess,this,_1));
    this->Handler->EventPaint.push_back(std::bind(&HandlersManager::Paint,this,_1,_2,_3,_4));
    this->Handler->EventSendTextResponce.push_back(std::bind(&HandlersManager::SendTextResponce,this,_1,_2));
    this->Handler->EventUrlLoaded.push_back(std::bind(&HandlersManager::UrlLoaded,this,_1,_2,_3));
    this->Handler->EventOldestRequestTimeChanged.push_back(std::bind(&HandlersManager::OldestRequestTimeChanged,this,_1,_2));

    this->Handler->EventPopupClosed.push_back(std::bind(&HandlersManager::PopupRemoved,this,_1));
    this->Handler->EventPopupCreated.push_back(std::bind(&HandlersManager::PopupCreated,this,_1,_2));
}

void HandlersManager::Init2(CefRefPtr<CefBrowser> Browser)
{
    OriginalHandler = std::make_shared<HandlerUnitClass>();
    OriginalHandler->Handler = this->Handler;
    OriginalHandler->Browser = Browser;
    OriginalHandler->BrowserId = Browser->GetIdentifier();
    OriginalHandler->IsActive = true;

    UpdateCurrent();
}

void HandlersManager::UpdateCurrent()
{
    int PrevBrowserId = CurrentBrowserId;
    bool IsPopupActive = false;

    if(OriginalHandler && OriginalHandler->ForceShow)
    {
        IsPopupActive = true;
        Handler = OriginalHandler->Handler;
        Browser = OriginalHandler->Browser;
    }

    if(!IsPopupActive)
    {
        for(HandlerUnit h:HandlerUnits)
        {
            if(h->ForceShow && h->IsActive && !h->DontUseAsActive && h->IsContextCreated)
            {
                IsPopupActive = true;
                Handler = h->Handler;
                Browser = h->Browser;
            }
        }
    }

    if(!IsPopupActive)
    {
        for(HandlerUnit h:HandlerUnits)
        {
            if(h->IsActive && !h->DontUseAsActive && h->IsContextCreated)
            {
                IsPopupActive = true;
                Handler = h->Handler;
                Browser = h->Browser;
            }
        }
    }

    if(!IsPopupActive && OriginalHandler)
    {
        Handler = OriginalHandler->Handler;
        Browser = OriginalHandler->Browser;
    }

    CurrentBrowserId = -1;
    if(Browser)
        CurrentBrowserId = Browser->GetIdentifier();

    if(PrevBrowserId != CurrentBrowserId && Browser)
    {
        Browser->GetHost()->Invalidate(PET_VIEW);
    }
}

MainHandler* HandlersManager::GetHandler()
{
    return Handler.get();
}

CefBrowser* HandlersManager::GetBrowser()
{
    return Browser.get();
}

void HandlersManager::Timer()
{
    std::vector<int> Ids;
    {
        LOCK_CONTEXT_LIST
        Ids = std::move(NewContextCreatedIds);
        NewContextCreatedIds.clear();
    }

    bool Updated = false;


    for(HandlerUnit h:HandlerUnits)
    {
        if(h->Handler.get() && h->IsActive && h->IsContextCreated)
            h->Handler->Timer();

        if(std::find(Ids.begin(), Ids.end(), h->BrowserId) != Ids.end())
        {
            h->IsContextCreated = true;
            Updated = true;
        }
    }

    if(OriginalHandler.get() && OriginalHandler->Handler.get())
        OriginalHandler->Handler->Timer();

    auto i = HandlerUnits.begin();
    while (i != HandlerUnits.end())
    {
        if(!(*i)->IsActive && (*i)->Handler->ref_count_.ref_count_ == 2 && (*i)->Handler->GetResourceListLength() == 0)
        {
            (*i)->Browser = 0;

            MainHandler *h = (*i)->Handler.get();
            (*i)->Handler = 0;
            delete h;

            i = HandlerUnits.erase(i);

            Updated = true;

            if(IsWaitForClosedCurrent)
                IsClosedCurrent = true;
        }else
        {
            MainHandler *h = (*i)->Handler.get();
            CefPostTask(TID_IO, base::Bind(&MainHandler::CleanResourceHandlerList, h));

            ++i;
        }
    }
    if(OriginalHandler)
    {
        MainHandler *h = OriginalHandler->Handler.get();
        CefPostTask(TID_IO, base::Bind(&MainHandler::CleanResourceHandlerList, h));
    }else
    {
        MainHandler *h = Handler.get();
        CefPostTask(TID_IO, base::Bind(&MainHandler::CleanResourceHandlerList, h));
    }
    if(Updated)
        UpdateCurrent();
}

void HandlersManager::Reset()
{
    for(HandlerUnit h:HandlerUnits)
    {
        h->DontUseAsActive = true;
        h->ForceShow = false;
        if(h->Browser)
            h->Browser->GetMainFrame()->ExecuteJavaScript("window.close();",h->Browser->GetMainFrame()->GetURL(),0);
    }
    if(OriginalHandler)
        OriginalHandler->ForceShow = false;

    UpdateCurrent();

}

void HandlersManager::PopupCreated(CefRefPtr<MainHandler> new_handler,CefRefPtr<CefBrowser> new_browser)
{
    HandlerUnit p = std::make_shared<HandlerUnitClass>();
    p->Handler = new_handler;
    p->Browser = new_browser;
    p->BrowserId = new_browser->GetIdentifier();
    p->IsActive = true;
    p->DontUseAsActive = false;


    p->Handler->EventLoadSuccess.clear();
    p->Handler->EventPaint.clear();
    p->Handler->EventSendTextResponce.clear();
    p->Handler->EventUrlLoaded.clear();
    p->Handler->EventPopupClosed.clear();
    p->Handler->EventPopupCreated.clear();
    p->Handler->EventOldestRequestTimeChanged.clear();


    p->Handler->EventLoadSuccess.push_back(std::bind(&HandlersManager::LoadSuccess,this,_1));
    p->Handler->EventPaint.push_back(std::bind(&HandlersManager::Paint,this,_1,_2,_3,_4));
    p->Handler->EventSendTextResponce.push_back(std::bind(&HandlersManager::SendTextResponce,this,_1,_2));
    p->Handler->EventUrlLoaded.push_back(std::bind(&HandlersManager::UrlLoaded,this,_1,_2,_3));
    p->Handler->EventOldestRequestTimeChanged.push_back(std::bind(&HandlersManager::OldestRequestTimeChanged,this,_1,_2));


    p->Handler->EventPopupClosed.push_back(std::bind(&HandlersManager::PopupRemoved,this,_1));
    p->Handler->EventPopupCreated.push_back(std::bind(&HandlersManager::PopupCreated,this,_1,_2));

    HandlerUnits.push_back(p);

    OriginalHandler->ForceShow = false;

    for(HandlerUnit ht:HandlerUnits)
        ht->ForceShow = false;

    UpdateCurrent();
}

void HandlersManager::PopupRemoved(int BrowserId)
{
    for(HandlerUnit h:HandlerUnits)
    {
        if(h->BrowserId == BrowserId)
        {
            h->IsActive = false;

            //if(DevToolsBorwserId == h->BrowserId)
            {
                for(auto f:EventNeedToCloseDevTools)
                    f();
            }
        }
    }
    UpdateCurrent();

}
void HandlersManager::SendTextResponce(const std::string& text, int BrowserId)
{
    if(CurrentBrowserId == BrowserId)
        SendTextResponceCallback(text);
}
void HandlersManager::UrlLoaded(const std::string& url, int status, int BrowserId)
{
    if(CurrentBrowserId == BrowserId)
        UrlLoadedCallback(url, status);
}
void HandlersManager::LoadSuccess(int BrowserId)
{
    if(CurrentBrowserId == BrowserId)
        LoadSuccessCallback();
}
void HandlersManager::Paint(char * data, int width, int height, int BrowserId)
{
    if(CurrentBrowserId == BrowserId)
        PaintCallback(data,width,height);
}

void HandlersManager::OldestRequestTimeChanged(int64 OldestTime, int BrowserId)
{
    if(CurrentBrowserId == BrowserId)
        OldestRequestTimeChangedCallback(OldestTime);
}

void HandlersManager::NewContextCreated(int ContextId)
{
    LOCK_CONTEXT_LIST
    NewContextCreatedIds.push_back(ContextId);
}

void HandlersManager::SetDevToolsBorwserId(int DevToolsBorwserId)
{
    this->DevToolsBorwserId = DevToolsBorwserId;
}

std::vector<std::string> HandlersManager::GetAllUrls()
{
    std::vector<std::string> res;
    if(OriginalHandler)
        res.push_back(OriginalHandler->Browser->GetMainFrame()->GetURL().ToString());
    for(HandlerUnit h:HandlerUnits)
    {
        if(!h->DontUseAsActive && h->IsContextCreated)
        {
            res.push_back(h->Browser->GetMainFrame()->GetURL().ToString());
        }
    }
    return res;
}

bool HandlersManager::CloseByIndex(int index)
{
    if(index <= 0 || index - 1 >= HandlerUnits.size())
        return false;

    IsWaitForClosedCurrent = true;
    IsClosedCurrent = false;

    HandlerUnit h = HandlerUnits[index - 1];

    h->DontUseAsActive = true;
    h->ForceShow = false;
    if(h->Browser)
    {
        h->Browser->GetMainFrame()->ExecuteJavaScript("window.close();",h->Browser->GetMainFrame()->GetURL(),0);
    }

    UpdateCurrent();
    return true;
}

void HandlersManager::SwitchByIndex(int index)
{
    HandlerUnit h;
    if(index <= 0)
    {
        h = OriginalHandler;
    }else if(index - 1 < HandlerUnits.size())
    {
        h = HandlerUnits[index - 1];
    }
    if(OriginalHandler)
        OriginalHandler->ForceShow = false;

    for(HandlerUnit ht:HandlerUnits)
        ht->ForceShow = false;

    if(h && !h->DontUseAsActive)
    {
        h->ForceShow = true;
    }

    UpdateCurrent();

}

bool HandlersManager::CheckIsClosed()
{
    if(IsWaitForClosedCurrent && IsClosedCurrent)
    {
        IsWaitForClosedCurrent = false;
        IsClosedCurrent = false;
        return true;
    }
    return false;
}
